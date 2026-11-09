/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file accounts.cpp Value added, expenditure and the explicit GDP discrepancy. */

#include "../stdafx.h"

#include "accounts.h"

#include "../cargotype.h"

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

static bool IsHouseholdService(const CargoSpec *spec)
{
	return spec->classes.Test(CargoClass::Passengers) || spec->classes.Test(CargoClass::Mail);
}

void EconomyLabNoteDelivery(CargoType cargo, int64_t units, int64_t tariff_pounds)
{
	if (units <= 0 || static_cast<uint8_t>(cargo) >= _lab.delivered.size()) return;
	const CargoSpec *spec = CargoSpec::Get(cargo);
	if (!spec->IsValid()) return;

	_lab.delivered[static_cast<size_t>(cargo)] += units;
	_lab.sequence++;
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
	_lab.subsidy_nominal += pounds;
	_lab.sequence++;
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

/**
 * Integer form of E-027 around the calibration month.
 * Y* / Y0 ≈ 1 + 0.30 (K/K0 − 1) + 0.70 (L/L0 − 1) + 0.20 (Logistics/Logistics0 − 1).
 * The weights are percent. This is not a silent copy of observed GDP.
 */
static int64_t PotentialReal(int64_t capital, int64_t population, int64_t capacity)
{
	if (_lab.anchored == 0 || _lab.potential_base <= 0) return 0;
	const int64_t capital_pm = RelativePermyriad(capital, _lab.capital_base);
	const int64_t labour_pm = RelativePermyriad(population, _lab.population_base);
	const int64_t logistics_pm = RelativePermyriad(capacity, _lab.capacity_base);
	int64_t scale = 10000;
	scale += 30 * (capital_pm - 10000) / 100;
	scale += 70 * (labour_pm - 10000) / 100;
	scale += 20 * (logistics_pm - 10000) / 100;
	if (scale < 1000) scale = 1000;
	return _lab.potential_base * scale / 10000;
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
		_lab.population_base = std::max<int64_t>(flows.population, 1);
		_lab.capacity_base = std::max<int64_t>(flows.capacity, 1);
		_lab.capital_base = capital > 0 ? capital : 0;
		_lab.anchored = 1;
	}
	accounts.potential = PotentialReal(capital, flows.population, flows.capacity);
	if (accounts.potential <= 0 && gdp_production > 0 && _lab.anchored != 0) accounts.potential = _lab.potential_base;

	const int64_t scale = _lab.potential_base > 0 ? _lab.potential_base : transport_go;
	const int64_t government_real = PolicyPointsToReal(flows.government_points, scale);
	accounts.government = government_real;
	const int64_t taxable = std::max<int64_t>(gdp_production, 0);
	accounts.tax_revenue = PolicyPointsToReal(flows.autonomous_tax_points, scale) + flows.tax_rate_percent * taxable / 100;
	accounts.gdp_expenditure = household_real + capex_real + government_real;
	accounts.discrepancy = gdp_production - accounts.gdp_expenditure;
	const int64_t ledger_debits_now = household_real + freight_real + opex_real + capex_real + subsidy_real + government_real;
	const int64_t ledger_credits_now = transport_go + opex_real + capex_real + subsidy_real + government_real;
	accounts.ledger_balanced = ledger_debits_now == ledger_credits_now;
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
	return _open;
}

static int64_t ClampPm(int64_t value)
{
	if (value < 2000) return 2000;
	if (value > 50000) return 50000;
	return value;
}

EconomyLabOpen EconomyLabApplyOpenEconomy(const EconomyLabAccounts &accounts, int32_t policy_rate_bp, int64_t price_index, int64_t public_debt, int64_t loan_pounds, int64_t population, int32_t unemployment_bp)
{
	if (price_index < 1) price_index = 1;
	if (_lab.price_base <= 0) _lab.price_base = price_index;
	if (_lab.exchange_pm <= 0) _lab.exchange_pm = 10000;
	if (_lab.fundamental_pm <= 0) _lab.fundamental_pm = 10000;

	const int64_t ppp = price_index * 10000 / _lab.price_base;
	const int64_t fundamental_gap = ppp - _lab.fundamental_pm;
	_lab.fundamental_pm += fundamental_gap / 24;
	if (fundamental_gap != 0 && fundamental_gap / 24 == 0) _lab.fundamental_pm += fundamental_gap > 0 ? 1 : -1;
	_lab.fundamental_pm = ClampPm(_lab.fundamental_pm);

	int64_t debt_pm = 0;
	if (accounts.gdp_production > 0 && public_debt > 0) {
		debt_pm = public_debt * 10000 / accounts.gdp_production;
		if (debt_pm > 100000) debt_pm = 100000;
	}
	_lab.risk_bp = static_cast<int32_t>(100 + debt_pm / 200);
	_lab.world_rate_bp = 200;

	/* E-053. A lower domestic rate raises the spot rate above the sticky fundamental. */
	const int64_t spread_bp = static_cast<int64_t>(policy_rate_bp) - _lab.world_rate_bp - _lab.risk_bp;
	int64_t adjust_pm = spread_bp * 2;
	if (adjust_pm > 8000) adjust_pm = 8000;
	if (adjust_pm < -8000) adjust_pm = -8000;
	_lab.exchange_pm = ClampPm(_lab.fundamental_pm * (10000 - adjust_pm) / 10000);

	int64_t real_rate = _lab.exchange_pm * _lab.price_base / price_index;
	real_rate = ClampPm(real_rate);
	const int64_t trade_base = _lab.potential_base > 0 ? _lab.potential_base / 10 : (accounts.gdp_production > 0 ? accounts.gdp_production / 10 : 0);
	int64_t activity_pm = 10000;
	if (_lab.potential_base > 0) {
		activity_pm = accounts.gdp_production * 10000 / _lab.potential_base;
		if (activity_pm < 0) activity_pm = 0;
		if (activity_pm > 20000) activity_pm = 20000;
	}
	_lab.exports = trade_base * real_rate / 10000;
	_lab.imports = trade_base * activity_pm / 10000 * 10000 / real_rate;

	const int64_t saving = accounts.gdp_production - accounts.consumption - accounts.tax_revenue;
	_lab.bank_deposits += saving;
	if (_lab.bank_deposits < 0) _lab.bank_deposits = 0;
	_lab.bank_loans = std::max<int64_t>(loan_pounds, 0) * _lab.price_base / price_index;
	_lab.bank_reserves = _lab.bank_deposits / 10;
	_lab.bank_bonds = public_debt > 0 ? std::min(public_debt, _lab.bank_deposits / 2) : 0;
	const int64_t assets = _lab.bank_loans + _lab.bank_reserves + _lab.bank_bonds;
	_lab.bank_equity = assets - _lab.bank_deposits;

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
	_open.bank_gap = assets - (_lab.bank_deposits + _lab.bank_equity);
	_open.employment = _lab.employment;
	_open.labour_force = _lab.labour_force;
	_open.overshooting = _lab.exchange_pm > _lab.fundamental_pm;
	const int64_t expenditure = accounts.consumption + accounts.investment + accounts.government + _lab.exports - _lab.imports;
	_open.discrepancy = accounts.gdp_production - expenditure;
	return _open;
}
