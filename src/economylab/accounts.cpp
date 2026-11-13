/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file accounts.cpp Value added, expenditure and the explicit GDP discrepancy. */

#include "../stdafx.h"

#include "accounts.h"
#include "laboratory.hpp"

#include "../cargotype.h"
#include "../settings_type.h"

#include <algorithm>

#include "../safeguards.h"

static EconomyLabPersistent _lab;

EconomyLabPersistent &EconomyLabPersistentState()
{
	return _lab;
}

void EconomyLabReset()
{
	_lab = {};
}

bool EconomyLabRecordPolicyShock(uint8_t variable, int32_t effective_tick, int64_t magnitude)
{
	if (_lab.policy_shock_count >= _lab.policy_shock_id.size()) return false;
	const uint8_t slot = _lab.policy_shock_count;
	_lab.policy_shock_id[slot] = static_cast<uint64_t>(slot) + 1;
	_lab.policy_shock_tick[slot] = effective_tick;
	_lab.policy_shock_magnitude[slot] = magnitude;
	_lab.policy_shock_variable[slot] = variable;
	_lab.policy_shock_count = static_cast<uint8_t>(slot + 1);
	return _lab.policy_shock_id[slot] != 0;
}

void EconomyLabRestoreFreshSession()
{
	const EconomyLabPersistent fresh;
	_lab.lab_tick = fresh.lab_tick;
	_lab.session_y = fresh.session_y;
	_lab.session_tax = fresh.session_tax;
	_lab.session_debt = fresh.session_debt;
	_lab.session_money = fresh.session_money;
	_lab.session_price = fresh.session_price;
	_lab.session_exchange = fresh.session_exchange;
	_lab.session_rate_bp = fresh.session_rate_bp;
	_lab.session_pd_bp = fresh.session_pd_bp;
	_lab.session_lgd = fresh.session_lgd;
	_lab.session_loans = fresh.session_loans;
	_lab.session_reserves = fresh.session_reserves;
	_lab.session_bonds = fresh.session_bonds;
	_lab.session_deposits = fresh.session_deposits;
	_lab.session_equity = fresh.session_equity;
	_lab.session_provision = fresh.session_provision;
	_lab.session_clamps = fresh.session_clamps;
	_lab.session_ok = fresh.session_ok;
	_lab.government_extra = fresh.government_extra;
	_lab.import_extra = fresh.import_extra;
	_lab.stream_shock = fresh.stream_shock;
	_lab.stream_trade = fresh.stream_trade;
	_lab.stream_bank = fresh.stream_bank;
	_lab.session_watermark = fresh.session_watermark;
	_lab.session_debits = fresh.session_debits;
	_lab.session_credits = fresh.session_credits;
	_lab.pending_count = 0;
	_lab.applied_count = 0;
	_lab.pending_id = {};
	_lab.pending_tick = {};
	_lab.pending_magnitude = {};
	_lab.pending_variable = {};
	_lab.pending_operation = {};
	_lab.applied_id = {};
	_lab.applied_tick = {};
	_lab.applied_magnitude = {};
	_lab.applied_variable = {};
	_lab.applied_operation = {};
	_lab.policy_shock_count = 0;
	_lab.policy_shock_id = {};
	_lab.policy_shock_tick = {};
	_lab.policy_shock_magnitude = {};
	_lab.policy_shock_variable = {};
}

static bool IsHouseholdService(const CargoSpec *spec)
{
	return spec->classes.Test(CargoClass::Passengers) || spec->classes.Test(CargoClass::Mail);
}

bool EconomyLabPostEvent(uint64_t id, int64_t amount)
{
	if (id == 0 || amount < 0 || id <= static_cast<uint64_t>(std::max<int64_t>(_lab.ledger_watermark, 0))) return false;
	_lab.ledger_debits += amount;
	_lab.ledger_credits += amount;
	_lab.ledger_watermark = static_cast<int64_t>(id);
	_lab.sequence = _lab.ledger_watermark;
	return _lab.ledger_debits == _lab.ledger_credits;
}

void EconomyLabNoteDelivery(CargoType cargo, int64_t units, int64_t tariff_pounds)
{
	if (units <= 0 || static_cast<uint8_t>(cargo) >= _lab.delivered.size()) return;
	const CargoSpec *spec = CargoSpec::Get(cargo);
	if (!spec->IsValid()) return;

	const int64_t amount = tariff_pounds > 0 ? tariff_pounds : 0;
	const uint64_t id = static_cast<uint64_t>(std::max<int64_t>(_lab.ledger_watermark, 0)) + 1;
	if (!EconomyLabPostEvent(id, amount)) return;
	_lab.delivered[static_cast<size_t>(cargo)] += units;
	if (tariff_pounds <= 0) return;
	if (IsHouseholdService(spec)) {
		_lab.household_nominal += tariff_pounds;
	} else {
		_lab.freight_nominal += tariff_pounds;
	}
}

void EconomyLabNoteSubsidy(int64_t pounds)
{
	if (pounds <= 0) return;
	const uint64_t id = static_cast<uint64_t>(std::max<int64_t>(_lab.ledger_watermark, 0)) + 1;
	if (!EconomyLabPostEvent(id, pounds)) return;
	_lab.subsidy_nominal += pounds;
}

/** Level relative to its base, in permyriad. Missing data stays at 10000 so it does not invent a gap. */
static int64_t RelativePermyriad(int64_t level, int64_t base)
{
	if (base <= 0 || level <= 0) return 10000;
	const int64_t ratio = level * 10000 / base;
	/* The linear form of E-027 is local. Keep the ratio inside one half to double. */
	if (ratio < 5000) return 5000;
	if (ratio > 20000) return 20000;
	return ratio;
}

/** 1000 policy points equal the calibration GDP, or this month's transport sales before that exists. */
static int64_t PolicyPointsToReal(int64_t points, int64_t scale)
{
	if (points <= 0 || scale <= 0) return 0;
	return points * scale / 1000;
}

/** Pounds of potential output. The policy scale stays on potential_base. */
static int64_t OutputLevel()
{
	if (_lab.output_level > 0) return _lab.output_level;
	return _lab.potential_base;
}

/**
 * Integer form of E-027.
 * Y* / Y0 ≈ 1 + 0.30 (K/K0 − 1) + 0.70 (L/L0 − 1) + 0.20 (Logistics/Logistics0 − 1).
 * The weights are percent. Y0 is the output level, not the policy-point scale.
 */
static int64_t FactorScale(int64_t capital, int64_t population, int64_t capacity, int32_t unemployment_bp)
{
	const int64_t labour_level = EconomyLabEffectiveLabour(population, unemployment_bp);
	const int64_t capital_pm = RelativePermyriad(capital, _lab.capital_base);
	const int64_t labour_pm = RelativePermyriad(labour_level, _lab.population_base);
	const int64_t logistics_pm = RelativePermyriad(capacity, _lab.capacity_base);
	int64_t scale = 10000;
	scale += 30 * (capital_pm - 10000) / 100;
	scale += 70 * (labour_pm - 10000) / 100;
	scale += 20 * (logistics_pm - 10000) / 100;
	if (scale < 1000) scale = 1000;
	return scale;
}

static int64_t PotentialReal(int64_t capital, int64_t population, int64_t capacity, int32_t unemployment_bp)
{
	const int64_t level = OutputLevel();
	if (_lab.anchored == 0 || level <= 0) return 0;
	return level * FactorScale(capital, population, capacity, unemployment_bp) / 10000;
}

bool EconomyLabRebaseOutputLevel(int64_t gdp, int64_t capital, int64_t population, int64_t capacity)
{
	(void)gdp;
	(void)capital;
	(void)population;
	(void)capacity;
	/* A-09. A gap outside the linear range is a calibration error. It is not a reason to copy GDP into potential. */
	return false;
}

EconomyLabAccounts EconomyLabCloseMonth(const EconomyLabFlows &flows, bool commit)
{
	EconomyLabAccounts accounts;
	const int64_t household_real = DeflatePounds(NominalPounds{_lab.household_nominal}, flows.payment_index).Raw();
	const int64_t freight_real = DeflatePounds(NominalPounds{_lab.freight_nominal}, flows.payment_index).Raw();
	const int64_t subsidy_real = DeflatePounds(NominalPounds{_lab.subsidy_nominal}, flows.payment_index).Raw();
	const int64_t capex_real = DeflatePounds(NominalPounds{std::max<int64_t>(flows.capex_pounds, 0)}, flows.payment_index).Raw();
	const int64_t opex_real = DeflatePounds(NominalPounds{std::max<int64_t>(flows.opex_pounds, 0)}, flows.payment_index).Raw();
	const int64_t transport_go = household_real + freight_real;

	const int64_t value_added = transport_go - opex_real;
	/* E-006. Product taxes are not observed yet. Product subsidies are the route payments. */
	const int64_t gdp_production = value_added - subsidy_real;
	accounts.gdp_production = gdp_production;
	accounts.consumption = household_real;
	accounts.investment = capex_real;
	accounts.subsidies = subsidy_real;
	accounts.intermediate = freight_real;
	accounts.value_added = value_added;

	int64_t pieces = 0;
	for (int64_t units : _lab.delivered) pieces += units;
	accounts.logistics_pieces = pieces;

	int64_t capital = flows.capital_stock;
	if (commit && _lab.generation == 0) {
		/* The stock inherited from the cargo-unit model is not in real pounds. */
		capital = 0;
		_lab.generation = 1;
		accounts.reset_legacy_capital = true;
	}
	if (commit && _lab.schema < 2) {
		/* Schema 1 priced policy points at 1,000 pounds and anchored capital at 1. */
		_lab.anchored = 0;
		_lab.potential_base = 0;
		_lab.output_level = 0;
		_lab.capital_base = 0;
		_lab.population_base = 0;
		_lab.capacity_base = 0;
		_lab.price_base = 0;
		_lab.exchange_pm = 10000;
		_lab.fundamental_pm = 10000;
		_lab.exports = 0;
		_lab.imports = 0;
		_lab.bank_deposits = 0;
		_lab.schema = 2;
		accounts.recalibrate = true;
	}

	if (commit && _lab.anchored == 0 && gdp_production > 0) {
		_lab.potential_base = gdp_production;
		_lab.output_level = gdp_production;
		_lab.population_base = std::max<int64_t>(flows.population, 1);
		_lab.capacity_base = std::max<int64_t>(flows.capacity, 1);
		_lab.capital_base = capital > 0 ? capital : 0;
		_lab.anchored = 1;
	}
	accounts.potential = PotentialReal(capital, flows.population, flows.capacity, flows.unemployment_bp);
	if (accounts.potential <= 0 && gdp_production > 0 && _lab.anchored != 0) accounts.potential = OutputLevel();
	accounts.level_rebased = false;

	const int64_t scale = _lab.potential_base > 0 ? _lab.potential_base : transport_go;
	const int64_t government_real = PolicyPointsToReal(flows.government_points, scale);
	accounts.government = government_real;
	const int64_t taxable = std::max<int64_t>(gdp_production, 0);
	accounts.tax_revenue = PolicyPointsToReal(flows.autonomous_tax_points, scale) + flows.tax_rate_percent * taxable / 100;
	accounts.gdp_expenditure = accounts.consumption + accounts.investment + accounts.government + _lab.exports - _lab.imports;
	accounts.discrepancy = lab::StatisticalDiscrepancy(gdp_production, accounts.consumption, accounts.investment, accounts.government, _lab.exports, _lab.imports);
	accounts.ledger_balanced = lab::IdentityHolds(gdp_production, accounts.consumption, accounts.investment, accounts.government, _lab.exports, _lab.imports, accounts.discrepancy);
	accounts.ok = accounts.ledger_balanced;

	if (commit) {
		_lab.household_nominal = 0;
		_lab.freight_nominal = 0;
		_lab.subsidy_nominal = 0;
		_lab.delivered.fill(0);
	}
	return accounts;
}

static EconomyLabOpen _open;

bool EconomyLabSealIdentity(const EconomyLabAccounts &accounts, int64_t exports, int64_t imports)
{
	const int64_t output = accounts.gdp_production + accounts.intermediate + accounts.subsidies;
	const int64_t discrepancy = lab::StatisticalDiscrepancy(accounts.gdp_production, accounts.consumption, accounts.investment, accounts.government, exports, imports);
	lab::Ledger ledger;
	const uint64_t watermark = static_cast<uint64_t>(std::max<int64_t>(_lab.ledger_watermark, 0));
	ledger.Restore(watermark);
	const uint64_t id = watermark + 1;
	const bool posted = output >= 0 && lab::PostNational(ledger, id, output, accounts.intermediate, accounts.subsidies, accounts.consumption, accounts.investment, accounts.government, std::max<int64_t>(exports, 0), std::max<int64_t>(imports, 0));
	if (!posted || !lab::IdentityHolds(accounts.gdp_production, accounts.consumption, accounts.investment, accounts.government, exports, imports, discrepancy)) return false;
	_lab.ledger_watermark = static_cast<int64_t>(id);
	_lab.sequence = _lab.ledger_watermark;
	_open.discrepancy = discrepancy;
	_open.identity_ok = true;
	return true;
}

const EconomyLabOpen &EconomyLabOpenState()
{
	_open.exports = _lab.exports;
	_open.imports = _lab.imports;
	_open.exchange_pm = _lab.exchange_pm > 0 ? _lab.exchange_pm : 10000;
	_open.fundamental_pm = _lab.fundamental_pm > 0 ? _lab.fundamental_pm : 10000;
	_open.world_rate_bp = _lab.world_rate_bp > 0 ? _lab.world_rate_bp : 200;
	_open.risk_bp = _lab.risk_bp;
	_open.loan_rate_bp = _lab.loan_rate_bp;
	_open.bank_loans = _lab.bank_loans;
	_open.bank_deposits = _lab.bank_deposits;
	_open.bank_reserves = _lab.bank_reserves;
	_open.bank_bonds = _lab.bank_bonds;
	_open.bank_equity = _lab.bank_equity;
	_open.bank_gap = (_lab.bank_loans + _lab.bank_reserves + _lab.bank_bonds) - (_lab.bank_deposits + _lab.bank_equity);
	_open.employment = _lab.employment;
	_open.labour_force = _lab.labour_force;
	_open.overshooting = _open.exchange_pm > _open.fundamental_pm;
	_open.world_gdp_pm = _lab.world_gdp_pm > 0 ? _lab.world_gdp_pm : 10000;
	_open.terms_of_trade_pm = _lab.terms_of_trade_pm > 0 ? _lab.terms_of_trade_pm : 10000;
	_open.external_debt = _lab.external_debt;
	_open.expected_devaluation_bp = _lab.expected_devaluation_bp;
	_open.external_service = _lab.external_debt * _open.world_rate_bp / 120000;
	_open.wages = _lab.wages;
	_open.dividends = _lab.dividends;
	_open.worker_consumption = _lab.worker_consumption;
	_open.owner_consumption = _lab.owner_consumption;
	_open.household_saving = _lab.household_saving;
	_open.portfolio_money = _lab.portfolio_money;
	_open.portfolio_private = _lab.portfolio_private;
	_open.portfolio_public = _lab.portfolio_public;
	_open.portfolio_external = _lab.portfolio_external;
	_open.funding_gap = _lab.funding_gap;
	_open.nfa = -_lab.external_debt;
	return _open;
}

static int64_t ClampPm(int64_t value)
{
	if (value < 2000) return 2000;
	if (value > 50000) return 50000;
	return value;
}

static int64_t Bound(int64_t value, int64_t lower, int64_t upper)
{
	if (value < lower) return lower;
	if (value > upper) return upper;
	return value;
}

int64_t EconomyLabEffectiveLabour(int64_t population, int32_t unemployment_bp)
{
	if (population < 0) population = 0;
	if (unemployment_bp < 0) unemployment_bp = 0;
	if (unemployment_bp > 10000) unemployment_bp = 10000;
	return population * (10000 - unemployment_bp) / 9500;
}

int64_t EconomyLabTermsOfTrade(int32_t date)
{
	int phase = date % 360;
	if (phase < 0) phase += 360;
	const int tri = phase < 180 ? phase : 360 - phase;
	return 10000 + static_cast<int64_t>(tri - 90) * 500 / 90;
}

int64_t EconomyLabGrowIndex(int64_t level, int32_t annual_bp, int64_t &remainder)
{
	if (level < 1) level = 1;
	if (annual_bp < 0) annual_bp = 0;
	remainder += level * static_cast<int64_t>(annual_bp);
	const int64_t step = remainder / 120000;
	remainder -= step * 120000;
	level += step;
	if (level < 1000) level = 1000;
	if (level > 1000000000) level = 1000000000;
	return level;
}

static int ClampPercent(int value)
{
	if (value < 0) return 0;
	if (value > 100) return 100;
	return value;
}

EconomyLabIncome EconomyLabDistributeIncome(int64_t gdp, int64_t interest_paid, int64_t tax, int wage_share_percent, int payout_percent, int worker_mpc_percent, int owner_mpc_percent)
{
	EconomyLabIncome out;
	if (gdp < 0) gdp = 0;
	if (interest_paid < 0) interest_paid = 0;
	if (tax < 0) tax = 0;
	wage_share_percent = ClampPercent(wage_share_percent);
	payout_percent = ClampPercent(payout_percent);
	worker_mpc_percent = ClampPercent(worker_mpc_percent);
	owner_mpc_percent = ClampPercent(owner_mpc_percent);

	out.wages = gdp * wage_share_percent / 100;
	out.profits = gdp - out.wages;
	int64_t after_interest = out.profits - interest_paid;
	if (after_interest < 0) after_interest = 0;
	out.dividends = after_interest * payout_percent / 100;

	int64_t tax_rate = 0;
	if (gdp > 0) {
		tax_rate = tax * 100 / gdp;
		if (tax_rate > 90) tax_rate = 90;
	}
	const int64_t wages_disp = out.wages * (100 - tax_rate) / 100;
	const int64_t dividends_disp = out.dividends * (100 - tax_rate) / 100;
	out.worker_consumption = wages_disp * worker_mpc_percent / 100;
	out.owner_consumption = dividends_disp * owner_mpc_percent / 100;
	out.worker_saving = wages_disp - out.worker_consumption;
	out.owner_saving = dividends_disp - out.owner_consumption;
	out.consumption = out.worker_consumption + out.owner_consumption;
	out.saving = out.worker_saving + out.owner_saving;
	return out;
}

EconomyLabPortfolio EconomyLabAllocateSaving(int64_t saving, int64_t money_gap, int64_t public_need, int32_t domestic_bp, int32_t foreign_bp)
{
	EconomyLabPortfolio out;
	const int64_t spread = static_cast<int64_t>(domestic_bp) - foreign_bp;
	int64_t foreign_share = 5000 - spread * 10;
	foreign_share = Bound(foreign_share, 0, 10000);

	int64_t money_flow = money_gap;
	if (saving >= 0 && money_flow > saving) money_flow = saving;
	if (saving < 0 && money_flow < saving) money_flow = saving;
	const int64_t residual = saving - money_flow;
	out.external_bonds = residual * foreign_share / 10000;
	const int64_t domestic = residual - out.external_bonds;
	if (public_need > 0 && domestic > 0) {
		out.public_bonds = public_need < domestic ? public_need : domestic;
	} else {
		out.public_bonds = 0;
	}
	out.private_bonds = domestic - out.public_bonds;
	out.money = money_flow;
	out.funding_gap = public_need - out.public_bonds;
	return out;
}

static int64_t PolicyScale()
{
	if (_lab.potential_base > 0) return _lab.potential_base;
	const int64_t level = OutputLevel();
	return level > 0 ? level : 1;
}

void EconomyLabAdvanceWorld(int32_t date, int64_t price_index)
{
	if (price_index < 1) price_index = 1;
	if (_lab.price_base <= 0) _lab.price_base = price_index;
	if (_lab.exchange_pm <= 0) _lab.exchange_pm = 10000;
	if (_lab.fundamental_pm <= 0) _lab.fundamental_pm = 10000;
	if (_lab.world_gdp_pm <= 0) _lab.world_gdp_pm = 10000;
	if (_lab.money_anchor <= 0 && _settings_game.economy.islm_money_supply > 0) {
		_lab.money_anchor = _settings_game.economy.islm_money_supply;
	}

	const int32_t growth = _settings_game.economy.islm_world_growth_bp;
	_lab.world_gdp_pm = EconomyLabGrowIndex(_lab.world_gdp_pm, growth, _lab.world_growth_remainder);

	const int64_t ppp = price_index * 10000 / _lab.price_base;
	const int64_t fundamental_gap = ppp - _lab.fundamental_pm;
	_lab.fundamental_pm += fundamental_gap / 24;
	if (fundamental_gap != 0 && fundamental_gap / 24 == 0) _lab.fundamental_pm += fundamental_gap > 0 ? 1 : -1;
	_lab.fundamental_pm = ClampPm(_lab.fundamental_pm);
	_lab.terms_of_trade_pm = EconomyLabTermsOfTrade(date);
}

static void TradeAt(int64_t local_gdp, int64_t price_index, int64_t exchange, int64_t terms, int64_t &exports, int64_t &imports)
{
	if (price_index < 1) price_index = 1;
	const int64_t base_price = _lab.price_base > 0 ? _lab.price_base : price_index;
	int64_t real_dollar = exchange * base_price / price_index;
	real_dollar = Bound(real_dollar, 2000, 50000);
	if (terms < 5000) terms = 5000;
	const int64_t world = _lab.world_gdp_pm > 0 ? _lab.world_gdp_pm : 10000;
	const int64_t level = OutputLevel();
	int64_t trade_base = level > 0 ? level / 10 : 0;
	if (trade_base <= 0 && local_gdp > 0) trade_base = local_gdp / 10;
	exports = trade_base * world / 10000 * real_dollar / 10000 * terms / 10000;
	int64_t activity = 10000;
	if (level > 0) activity = Bound(local_gdp * 10000 / level, 0, 20000);
	const int64_t dollar = real_dollar > 0 ? real_dollar : 1;
	imports = trade_base * activity / 10000 * 10000 / dollar * 10000 / terms;
	if (exports < 0) exports = 0;
	if (imports < 0) imports = 0;
}

EconomyLabDemand EconomyLabPreviewDemand(const EconomyLabAccounts &accounts, int32_t policy_rate_bp, int64_t price_index, int64_t public_debt)
{
	EconomyLabDemand out;
	if (price_index < 1) price_index = 1;
	const EconomySettings &cfg = _settings_game.economy;
	const int64_t scale = PolicyScale();
	const int64_t base_price = _lab.price_base > 0 ? _lab.price_base : price_index;
	const int64_t anchor = _lab.money_anchor > 0 ? _lab.money_anchor : (cfg.islm_money_supply > 0 ? cfg.islm_money_supply : 1);
	const int64_t real_points = static_cast<int64_t>(cfg.islm_money_supply) * base_price / price_index;
	const int64_t potential = accounts.potential > 0 ? accounts.potential : (accounts.gdp_production > 0 ? accounts.gdp_production : scale);
	int64_t activity = 10000;
	if (potential > 0) activity = Bound(accounts.gdp_production * 10000 / potential, 0, 20000);
	int64_t income_points = anchor * activity / 10000;
	if (cfg.islm_money_income_percent > 0) income_points = income_points * cfg.islm_money_income_percent / 50;
	if (income_points < 1) income_points = 1;
	int64_t excess_pm = (real_points - income_points) * 10000 / anchor;
	excess_pm = Bound(excess_pm, -2000, 2000);
	out.real_money = real_points;
	out.money_demand = income_points;
	out.money_impulse = potential * excess_pm / 10000;

	const int64_t per_point = cfg.islm_investment_sensitivity * scale / 1000;
	out.interest_effect = -per_point * (static_cast<int64_t>(policy_rate_bp) - 400) / 100;
	out.interest_effect = Bound(out.interest_effect, -scale / 5, scale / 5);

	int64_t debt_pm = 0;
	if (accounts.gdp_production > 0 && public_debt > 0) {
		debt_pm = Bound(public_debt * 10000 / accounts.gdp_production, 0, 100000);
	}
	int64_t carried_premium = 0;
	if (accounts.gdp_production > 0 && _lab.funding_gap > 0) {
		carried_premium = _lab.funding_gap * 10000 / accounts.gdp_production / 10;
	}
	out.world_rate_bp = cfg.islm_world_interest_bp;
	const int32_t risk_bp = static_cast<int32_t>(Bound(100 + debt_pm / 200 + carried_premium, 0, 2000));
	const int64_t spread_bp = static_cast<int64_t>(policy_rate_bp) - out.world_rate_bp - risk_bp;
	int64_t adjust_pm = Bound(spread_bp * 2, -8000, 8000);
	const int64_t fundamental = _lab.fundamental_pm > 0 ? _lab.fundamental_pm : 10000;
	const int64_t rate_target = ClampPm(fundamental * (10000 - adjust_pm) / 10000);
	const int64_t terms = _lab.terms_of_trade_pm > 0 ? _lab.terms_of_trade_pm : 10000;
	int64_t spot = _lab.exchange_pm > 0 ? _lab.exchange_pm : 10000;
	int64_t exports = 0;
	int64_t imports = 0;
	const int64_t gdp_for_gap = accounts.gdp_production > 0 ? accounts.gdp_production : 1;
	for (int pass = 0; pass < 3; pass++) {
		TradeAt(accounts.gdp_production, price_index, spot, terms, exports, imports);
		const int64_t service = _lab.external_debt * out.world_rate_bp / 120000;
		const int64_t current_account = exports - imports - service;
		int64_t expected = -current_account * 10000 / gdp_for_gap;
		expected = Bound(expected, -2000, 2000);
		expected = Bound(expected + _lab.parity_pressure_bp, -2000, 2000);
		out.expected_devaluation_bp = static_cast<int32_t>(expected);
		out.external_service = service;
		spot = ClampPm(rate_target * (10000 + expected / 12) / 10000);
	}
	TradeAt(accounts.gdp_production, price_index, spot, terms, exports, imports);
	out.exports = exports;
	out.imports = imports;
	out.exchange_pm = spot;
	out.terms_of_trade_pm = terms;
	out.world_gdp_pm = _lab.world_gdp_pm > 0 ? _lab.world_gdp_pm : 10000;
	const int32_t charged_bp = static_cast<int32_t>(Bound(static_cast<int64_t>(policy_rate_bp) + risk_bp, 0, 2500));
	const int64_t loan_interest = _lab.bank_loans > 0 ? _lab.bank_loans * charged_bp / 120000 : 0;
	const EconomyLabIncome split = EconomyLabDistributeIncome(accounts.gdp_production, loan_interest, accounts.tax_revenue,
		cfg.islm_wage_share_percent, cfg.islm_dividend_payout_percent, cfg.islm_mpc_percent, cfg.islm_owner_mpc_percent);
	out.wages = split.wages;
	out.dividends = split.dividends;
	out.worker_consumption = split.worker_consumption;
	out.owner_consumption = split.owner_consumption;
	out.saving = split.saving;
	const int64_t autonomous_c = PolicyPointsToReal(cfg.islm_autonomous_consumption, scale);
	const int64_t autonomous_i = PolicyPointsToReal(cfg.islm_autonomous_investment, scale);
	out.consumption = split.consumption + autonomous_c + out.money_impulse;
	out.investment = accounts.investment + autonomous_i + out.interest_effect;
	out.demand = out.consumption + out.investment + accounts.government + exports - imports;
	return out;
}

int32_t EconomyLabRateAtOutput(int64_t output, int64_t price_index)
{
	if (price_index < 1) price_index = 1;
	const EconomySettings &cfg = _settings_game.economy;
	const int64_t base_price = _lab.price_base > 0 ? _lab.price_base : price_index;
	const int64_t anchor = _lab.money_anchor > 0 ? _lab.money_anchor : (cfg.islm_money_supply > 0 ? cfg.islm_money_supply : 1);
	const int64_t real_points = static_cast<int64_t>(cfg.islm_money_supply) * base_price / price_index;
	const int64_t potential = OutputLevel() > 0 ? OutputLevel() : 1;
	const int64_t activity = Bound(output * 10000 / potential, 0, 20000);
	int64_t income_points = anchor * activity / 10000;
	if (cfg.islm_money_income_percent > 0) income_points = income_points * cfg.islm_money_income_percent / 50;
	int64_t slope = cfg.islm_money_interest_sensitivity;
	if (slope < 1) slope = 1;
	const int64_t rate = 400 + (income_points - real_points) * 100 / slope;
	return static_cast<int32_t>(Bound(rate, 0, 2500));
}

EconomyLabOpen EconomyLabApplyOpenEconomy(const EconomyLabAccounts &accounts, int32_t policy_rate_bp, int64_t price_index, int64_t public_debt, int64_t loan_pounds, int64_t population, int32_t unemployment_bp)
{
	if (price_index < 1) price_index = 1;
	if (_lab.price_base <= 0) _lab.price_base = price_index;
	if (_lab.exchange_pm <= 0) _lab.exchange_pm = 10000;
	if (_lab.fundamental_pm <= 0) _lab.fundamental_pm = 10000;

	const EconomyLabDemand priced = EconomyLabPreviewDemand(accounts, policy_rate_bp, price_index, public_debt);
	_lab.exchange_pm = priced.exchange_pm;
	_lab.exports = priced.exports;
	_lab.imports = priced.imports;
	_lab.world_rate_bp = priced.world_rate_bp;
	_lab.expected_devaluation_bp = priced.expected_devaluation_bp;
	_lab.external_debt += priced.imports - priced.exports + priced.external_service;
	_lab.external_debt = Bound(_lab.external_debt, -1000000000000, 1000000000000);
	_lab.wages = priced.wages;
	_lab.dividends = priced.dividends;
	_lab.worker_consumption = priced.worker_consumption;
	_lab.owner_consumption = priced.owner_consumption;
	_lab.household_saving = priced.saving;
	const int64_t treasury_interest = public_debt > 0 ? public_debt * (policy_rate_bp > 0 ? policy_rate_bp : 0) / 120000 : 0;
	const int64_t public_need = accounts.government - accounts.tax_revenue + treasury_interest;
	const int32_t foreign_bp = priced.world_rate_bp + priced.expected_devaluation_bp;
	const EconomyLabPortfolio placed = EconomyLabAllocateSaving(priced.saving, -priced.money_impulse, public_need, policy_rate_bp, foreign_bp);
	_lab.portfolio_money = placed.money;
	_lab.portfolio_private = placed.private_bonds;
	_lab.portfolio_public = placed.public_bonds;
	_lab.portfolio_external = placed.external_bonds;
	_lab.funding_gap = placed.funding_gap;
	const int64_t current_account = priced.exports - priced.imports - priced.external_service;
	if (accounts.gdp_production > 0) {
		int64_t parity_gap = (placed.external_bonds - current_account) * 10000 / accounts.gdp_production;
		_lab.parity_pressure_bp = static_cast<int32_t>(Bound(parity_gap, -1000, 1000));
	} else {
		_lab.parity_pressure_bp = 0;
	}
	int64_t debt_pm = 0;
	if (accounts.gdp_production > 0 && public_debt > 0) {
		debt_pm = Bound(public_debt * 10000 / accounts.gdp_production, 0, 100000);
	}
	int64_t funding_premium = 0;
	if (accounts.gdp_production > 0 && placed.funding_gap > 0) {
		funding_premium = placed.funding_gap * 10000 / accounts.gdp_production / 10;
	}
	_lab.risk_bp = static_cast<int32_t>(Bound(100 + debt_pm / 200 + funding_premium, 0, 2000));

	const int64_t saving = priced.saving;
	const int64_t loans_now = std::max<int64_t>(loan_pounds, 0) * _lab.price_base / price_index;
	if (_lab.opening_booked == 0) {
		_lab.bank_loans = loans_now;
		/* Outstanding public debt starts in the banks. Households buy their share after the deposit inflow. */
		if (public_debt > 0) _lab.bank_bonds += public_debt;
		_lab.treasury_debt = public_debt;
		_lab.household_bonds = public_debt < 0 ? public_debt : 0;
		_lab.bank_equity = _lab.bank_loans + _lab.bank_reserves + _lab.bank_bonds - _lab.bank_deposits;
		_lab.base_money = _lab.cb_bonds + _lab.official_reserves - _lab.cb_reserve_liability;
		_lab.opening_booked = 1;
	}
	lab::Books books;
	books.loans = _lab.bank_loans;
	books.reserves = _lab.bank_reserves;
	books.bank_bonds = _lab.bank_bonds;
	books.deposits = _lab.bank_deposits;
	books.equity = _lab.bank_equity;
	books.public_debt = _lab.treasury_debt;
	books.cb_bonds = _lab.cb_bonds;
	books.cb_reserve_liability = _lab.cb_reserve_liability;
	books.official_reserves = _lab.official_reserves;
	books.base_money = _lab.base_money;
	books.household_bonds = _lab.household_bonds;
	books.nfa = _lab.nfa;
	/* The trade balance is carried by external bonds, not by a second reserve stock. */
	const int64_t capital_inflow = _lab.imports - _lab.exports;
	books = lab::AdvanceBooks(books, saving, loans_now - _lab.bank_loans, public_debt - _lab.treasury_debt, _lab.exports, _lab.imports, capital_inflow);
	/* Households buy the public bonds they were allocated. The purchase leaves deposits and the banks' bond book. */
	int64_t bought = placed.public_bonds;
	if (bought < 0) bought = 0;
	if (bought > books.bank_bonds) bought = books.bank_bonds;
	if (bought > books.deposits) bought = books.deposits > 0 ? books.deposits : 0;
	books.bank_bonds -= bought;
	books.deposits -= bought;
	books.household_bonds += bought;
	books.bank_gap = (books.loans + books.reserves + books.bank_bonds) - (books.deposits + books.equity + books.provision);
	books.treasury_gap = books.public_debt - (books.bank_bonds + books.cb_bonds + books.household_bonds);
	_lab.bank_loans = books.loans;
	_lab.bank_reserves = books.reserves;
	_lab.bank_bonds = books.bank_bonds;
	_lab.bank_deposits = books.deposits;
	_lab.bank_equity = books.equity;
	_lab.treasury_debt = books.public_debt;
	_lab.cb_bonds = books.cb_bonds;
	_lab.cb_reserve_liability = books.cb_reserve_liability;
	_lab.official_reserves = books.official_reserves;
	_lab.base_money = books.base_money;
	_lab.household_bonds = books.household_bonds;
	_lab.nfa = -_lab.external_debt;

	_lab.labour_force = population > 0 ? population * 45 / 100 : 0;
	int32_t u_bp = unemployment_bp;
	if (u_bp < 0) u_bp = 0;
	if (u_bp > 10000) u_bp = 10000;
	_lab.employment = _lab.labour_force * (10000 - u_bp) / 10000;

	int32_t loan_rate = policy_rate_bp + _lab.risk_bp;
	if (loan_rate < 0) loan_rate = 0;
	if (loan_rate > 2500) loan_rate = 2500;
	_lab.loan_rate_bp = loan_rate;

	_open.exports = _lab.exports;
	_open.imports = _lab.imports;
	_open.exchange_pm = _lab.exchange_pm;
	_open.fundamental_pm = _lab.fundamental_pm;
	_open.world_rate_bp = _lab.world_rate_bp;
	_open.risk_bp = _lab.risk_bp;
	_open.loan_rate_bp = _lab.loan_rate_bp;
	_open.bank_loans = _lab.bank_loans;
	_open.bank_deposits = _lab.bank_deposits;
	_open.bank_reserves = _lab.bank_reserves;
	_open.bank_bonds = _lab.bank_bonds;
	_open.bank_equity = _lab.bank_equity;
	_open.bank_gap = books.bank_gap;
	_open.treasury_gap = books.treasury_gap;
	_open.cb_gap = books.cb_gap;
	_open.external_gap = books.external_gap;
	_open.base_money = books.base_money;
	_open.official_reserves = books.official_reserves;
	_open.household_bonds = books.household_bonds;
	_open.external_debt = _lab.external_debt;
	_open.nfa = _lab.nfa;
	_open.wages = _lab.wages;
	_open.dividends = _lab.dividends;
	_open.worker_consumption = _lab.worker_consumption;
	_open.owner_consumption = _lab.owner_consumption;
	_open.household_saving = _lab.household_saving;
	_open.portfolio_money = _lab.portfolio_money;
	_open.portfolio_private = _lab.portfolio_private;
	_open.portfolio_public = _lab.portfolio_public;
	_open.portfolio_external = _lab.portfolio_external;
	_open.funding_gap = _lab.funding_gap;
	_open.expected_devaluation_bp = _lab.expected_devaluation_bp;
	_open.employment = _lab.employment;
	_open.labour_force = _lab.labour_force;
	_open.overshooting = _lab.exchange_pm > _lab.fundamental_pm;
	EconomyLabAccounts posted = accounts;
	posted.consumption = priced.consumption;
	posted.investment = priced.investment;
	_open.identity_ok = EconomyLabSealIdentity(posted, _lab.exports, _lab.imports) && lab::BooksReconcile(books);
	return _open;
}
