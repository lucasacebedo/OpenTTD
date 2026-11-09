/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy.cpp Monthly accounts, Taylor rule and expectations-augmented Phillips curve. */

#include "stdafx.h"

#include "macro_economy.h"
#include "economylab/accounts.h"
#include "company_base.h"
#include "economy_func.h"
#include "rail.h"
#include "road.h"
#include "road_func.h"
#include "station_func.h"
#include "water.h"
#include "genworld.h"
#include "news_func.h"
#include "strings_func.h"
#include "timer/timer_game_economy.h"
#include "town.h"
#include "vehicle_base.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/*
 * Closed economy, fixed-point and deterministic (no floating point).
 * The identities live in economylab/accounts.cpp and docs/model/equations.yaml.
 *
 * GDP is transport value added at 1920 pounds, minus route subsidies.
 * It is not a sum of cargo pieces. Passenger and mail tariffs are consumption.
 * Other tariffs are intermediate purchases. Construction and new vehicles are
 * investment. Maintenance is intermediate consumption. Government purchases are
 * the purchase slider converted at POLICY_POINT_TO_REAL_POUNDS. The gap between
 * production and expenditure is stored and shown; it is not written into consumption.
 *
 * The policy rate is the Taylor rule E-032. The loan rate charged to companies
 * is the non-negative policy rate. Potential output follows the integer form of
 * E-027 and is calibrated once. It is not copied from GDP when the two differ.
 */

static constexpr int64_t MACRO_PRICE_SCALE = 100; ///< Stored price index is the displayed index times this.
static constexpr int64_t MACRO_MIN_PRICE = 100 * MACRO_PRICE_SCALE;
static constexpr int64_t MACRO_MAX_PRICE = 200000 * MACRO_PRICE_SCALE;
static constexpr int64_t MACRO_POUNDS_PER_POINT = 50000; ///< 1920 pounds of company loans that count as one point of the private-debt stock.
static constexpr int64_t MACRO_CAPITAL_DEPRECIATION = 600; ///< Months to depreciate one part in this many of the capital stock, about 2 percent a year.
static constexpr int64_t MACRO_OKUN_NATURAL_BP = 500; ///< Unemployment at a zero output gap, 5 percent.

struct IsLmResult {
	bool ok = false;
	int32_t output = 0;
	int32_t interest_bp = 0;
	int32_t consumption = 0;
	int32_t investment = 0;
	int32_t tax_revenue = 0;
	int32_t real_money = 0;
	int32_t output_gap_bp = 0;
	int32_t unemployment_bp = 0;
	int32_t game_output = 0;
	int32_t exogenous_output = 0;
	int32_t money_demand = 0;
	int32_t debt_price = 0;
	int32_t leakage = 3600;
};

static int32_t ClampI32(int64_t value, int64_t lower, int64_t upper)
{
	if (value < lower) return static_cast<int32_t>(lower);
	if (value > upper) return static_cast<int32_t>(upper);
	return static_cast<int32_t>(value);
}

static int32_t RoundHundredthsToPercent(int32_t hundredths)
{
	if (hundredths >= 0) return (hundredths + 50) / 100;
	return -((-hundredths + 50) / 100);
}

static int32_t PhillipsInflation(int32_t expected_bp, int32_t output_gap_bp);

/** Neutral nominal rate in the Taylor rule, before the output gap. r* and the inflation target are 2 percent. */
static int64_t TaylorNeutralBp(int32_t inflation_bp)
{
	constexpr int64_t r_star_bp = 200;
	constexpr int64_t pi_target_bp = 200;
	constexpr int64_t phi_pi = 150; ///< 1.50 points of the rate per point of inflation above target.
	return r_star_bp + pi_target_bp + phi_pi * (static_cast<int64_t>(inflation_bp) - pi_target_bp) / 100;
}

/** E-032 without the exchange-rate term. The safety band is 0 to 25 percent, not −50 to +100. */
static int32_t TaylorRateBp(int32_t inflation_bp, int32_t gap_bp)
{
	constexpr int64_t phi_y = 50; ///< 0.50 points of the rate per point of output gap.
	const int64_t rate = TaylorNeutralBp(inflation_bp) + phi_y * gap_bp / 100;
	return ClampI32(rate, 0, 2500);
}

/** Publish one monthly close. Consumption is measured; the discrepancy is kept apart. */
static IsLmResult PublishAccounts(const EconomyLabAccounts &accounts, int32_t price_index)
{
	const EconomySettings &cfg = _settings_game.economy;
	IsLmResult result;
	constexpr int64_t kShown = 2000000000;

	const int64_t output = accounts.gdp_production;
	const int64_t potential = accounts.potential;
	int32_t gap_bp = 0;
	if (potential > 0) gap_bp = ClampI32((output - potential) * 10000 / potential, -10000, 10000);

	const int32_t inflation_bp = PhillipsInflation(_economy.macro_expected_inflation_bp, gap_bp);
	const int32_t interest_bp = TaylorRateBp(inflation_bp, gap_bp);
	const int64_t yield_bp = std::max<int32_t>(interest_bp, 1);
	const int64_t price = std::clamp<int64_t>(price_index, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
	const int64_t real_money = static_cast<int64_t>(cfg.islm_money_supply) * 1000 * MACRO_PRICE_SCALE / price;

	result.ok = accounts.ledger_balanced;
	result.output = ClampI32(output, -kShown, kShown);
	result.interest_bp = interest_bp;
	result.consumption = ClampI32(accounts.consumption, 0, kShown);
	result.investment = ClampI32(accounts.investment, 0, kShown);
	result.tax_revenue = ClampI32(accounts.tax_revenue, 0, kShown);
	result.real_money = ClampI32(real_money, 0, kShown);
	result.money_demand = 0;
	result.game_output = ClampI32(accounts.logistics_pieces, 0, kShown);
	result.exogenous_output = ClampI32(accounts.discrepancy, -kShown, kShown);
	result.debt_price = ClampI32(500 * 1000 / yield_bp, 1, kShown);
	result.output_gap_bp = gap_bp;
	/* Bridge until the labour module. A one-point boom cuts unemployment by half a point. */
	result.unemployment_bp = ClampI32(MACRO_OKUN_NATURAL_BP - gap_bp / 2, 0, 5000);
	result.leakage = 0;
	return result;
}

static int32_t PhillipsInflation(int32_t expected_bp, int32_t output_gap_bp)
{
	const int64_t slope = _settings_game.economy.islm_phillips_slope_percent;
	return ClampI32(static_cast<int64_t>(expected_bp) + slope * output_gap_bp / 100, -5000, 10000);
}

static void StoreEquilibrium(const IsLmResult &result, int32_t inflation_bp)
{
	_economy.macro_ok = result.ok ? 1 : 0;
	if (!result.ok) return;

	_economy.macro_output = result.output;
	_economy.macro_interest_bp = result.interest_bp;
	_economy.macro_output_gap_bp = result.output_gap_bp;
	_economy.macro_consumption = result.consumption;
	_economy.macro_investment = result.investment;
	_economy.macro_tax_revenue = result.tax_revenue;
	_economy.macro_real_money = result.real_money;
	_economy.macro_game_output = result.game_output;
	_economy.macro_exogenous_output = result.exogenous_output;
	_economy.macro_money_demand = result.money_demand;
	_economy.macro_unemployment_bp = result.unemployment_bp;
	_economy.macro_private_debt_price = result.debt_price;
	_economy.macro_inflation_bp = inflation_bp;

	const int32_t charged = std::max<int32_t>(result.interest_bp, 0);
	_economy.interest_rate = static_cast<uint8_t>(std::clamp(charged / 100, 0, 255));
}

static void ApplyIndexStep(uint64_t &index, int32_t annual_bp)
{
	if (annual_bp == 0 || index == 0) return;

	int64_t next = static_cast<int64_t>(index) + static_cast<int64_t>(index) * annual_bp / 120000;
	if (next < (1 << 15)) next = 1 << 15;
	if (next > static_cast<int64_t>(MAX_INFLATION)) next = static_cast<int64_t>(MAX_INFLATION);
	index = static_cast<uint64_t>(next);
}

/** Advance prices, the price index and inflation expectations by one economy month. */
static void AdvancePhillipsCurve(int32_t inflation_bp)
{
	const EconomySettings &cfg = _settings_game.economy;
	const int32_t payment_bp = ClampI32(static_cast<int64_t>(inflation_bp) * cfg.islm_payment_inflation_percent / 100, -5000, 10000);

	ApplyIndexStep(_economy.inflation_prices, inflation_bp);
	ApplyIndexStep(_economy.inflation_payment, payment_bp);

	int64_t price = _economy.macro_price_index;
	price += price * inflation_bp / 120000;
	_economy.macro_price_index = ClampI32(price, MACRO_MIN_PRICE, MACRO_MAX_PRICE);

	const int64_t expected = _economy.macro_expected_inflation_bp;
	const int64_t surprise = static_cast<int64_t>(inflation_bp) - expected;
	_economy.macro_expected_inflation_bp = ClampI32(expected + surprise * cfg.islm_expectation_adaptation_percent / 1200, -5000, 10000);

	const int32_t price_percent = std::clamp(RoundHundredthsToPercent(std::max(inflation_bp, 0)), 0, 255);
	const int32_t payment_percent = std::clamp(price_percent * cfg.islm_payment_inflation_percent / 100, 0, 255);
	_economy.infl_amount = static_cast<uint8_t>(price_percent);
	_economy.infl_amount_pr = static_cast<uint8_t>(payment_percent);
	/* Next month's consumption sees the inflation that has now happened. */
	_economy.macro_lagged_inflation_bp = inflation_bp;
}

static void PublishMacroState()
{
	InvalidateWindowData(WindowClass::MacroEconomy, 0);
	InvalidateWindowData(WindowClass::MacroDashboard, 0);
	InvalidateWindowData(WindowClass::MacroFlowGraph, 0);
	InvalidateWindowData(WindowClass::MacroActivityGraph, 0);
	InvalidateWindowData(WindowClass::MacroMoneyGraph, 0);
	InvalidateWindowData(WindowClass::MacroIsLmGraph, 0);
	InvalidateWindowData(WindowClass::MacroAsAdGraph, 0);
	InvalidateWindowData(WindowClass::MacroLedger, 0);
	InvalidateWindowClassesData(WindowClass::Finances);
}

static int64_t TownPopulation()
{
	int64_t population = 0;
	for (const Town *town : Town::Iterate()) population += town->cache.population;
	return population;
}

static int64_t InstalledCargoCapacity()
{
	int64_t capacity = 0;
	for (const Vehicle *vehicle : Vehicle::Iterate()) capacity += vehicle->cargo_cap;
	return capacity;
}

/** Pounds the companies have booked this economy year for construction and new vehicles. Costs are stored negative. */
static int64_t CompanyCapitalSpending()
{
	int64_t spent = 0;
	for (const Company *company : Company::Iterate()) {
		spent -= static_cast<int64_t>(company->yearly_expenses[0][ExpensesType::Construction]);
		spent -= static_cast<int64_t>(company->yearly_expenses[0][ExpensesType::NewVehicles]);
	}
	return spent;
}

/** What it costs to keep the rails, roads, signals, canals, stations and airports the companies already own. */
static int64_t InfrastructureOperatingCost()
{
	int64_t total = 0;
	for (const Company *company : Company::Iterate()) {
		const uint32_t rail_total = company->infrastructure.GetRailTotal();
		for (RailType railtype : EnumRange(RAILTYPE_END)) {
			if (company->infrastructure.rail[railtype] != 0) {
				total += static_cast<int64_t>(RailMaintenanceCost(railtype, company->infrastructure.rail[railtype], rail_total));
			}
		}
		total += static_cast<int64_t>(SignalMaintenanceCost(company->infrastructure.signal));
		const uint32_t road_total = company->infrastructure.GetRoadTotal();
		const uint32_t tram_total = company->infrastructure.GetTramTotal();
		for (RoadType roadtype : EnumRange(ROADTYPE_END)) {
			if (company->infrastructure.road[roadtype] == 0) continue;
			const uint32_t group = RoadTypeIsRoad(roadtype) ? road_total : tram_total;
			total += static_cast<int64_t>(RoadMaintenanceCost(roadtype, company->infrastructure.road[roadtype], group));
		}
		total += static_cast<int64_t>(CanalMaintenanceCost(company->infrastructure.water));
		total += static_cast<int64_t>(StationMaintenanceCost(company->infrastructure.station));
		total += static_cast<int64_t>(AirportMaintenanceCost(company->index));
	}
	return std::max<int64_t>(total, 0);
}

void MacroNoteCargoDelivery(CargoType cargo, int64_t units, int64_t tariff_pounds)
{
	if (!_settings_game.economy.islm_enabled || _game_mode == GameMode::Menu) return;
	EconomyLabNoteDelivery(cargo, units, tariff_pounds);
}

struct GameFlows {
	int64_t capex = 0;
	int64_t opex = 0;
};

/** Construction and new vehicles are investment. Maintenance is intermediate consumption. */
static GameFlows MeasureGameFlows(bool close_month)
{
	GameFlows flows;
	const int64_t capex = CompanyCapitalSpending();
	int64_t capex_flow = capex - _economy.macro_company_spent_year;
	if (capex_flow < 0) {
		int64_t previous = 0;
		for (const Company *company : Company::Iterate()) {
			previous -= static_cast<int64_t>(company->yearly_expenses[1][ExpensesType::Construction]);
			previous -= static_cast<int64_t>(company->yearly_expenses[1][ExpensesType::NewVehicles]);
		}
		capex_flow = previous + capex - _economy.macro_company_spent_year;
		if (capex_flow < 0) capex_flow = std::max<int64_t>(capex, 0);
	}
	if (close_month) _economy.macro_company_spent_year = capex;

	flows.capex = std::max<int64_t>(capex_flow, 0);
	flows.opex = InfrastructureOperatingCost();
	return flows;
}

static void UpdateCargoPotential();

/** Refresh the population and cargo capacity the window shows. Does not move potential output. */
void MacroEconomyObserve()
{
	if (_generating_world || _game_mode == GameMode::Menu || !_settings_game.economy.islm_enabled) return;

	_economy.macro_population = TownPopulation();
	_economy.macro_cargo_capacity = InstalledCargoCapacity();
}

/**
 * One economy month of the supply side. Company spending enters demand and the
 * capital stock. Potential output takes one step toward the level implied by
 * population, installed cargo capacity and that capital stock.
 */
static void AdvanceAggregateSupply()
{
	if (_generating_world) return;

	const int64_t population = TownPopulation();
	const int64_t capacity = InstalledCargoCapacity();
	if (_economy.macro_population_snapshot > 0) {
		_economy.macro_population_growth_bp = ClampI32((population - _economy.macro_population_snapshot) * 10000 / _economy.macro_population_snapshot, -5000, 5000);
	}
	_economy.macro_population = population;
	_economy.macro_population_snapshot = population;
	_economy.macro_cargo_capacity = capacity;
	if (_economy.macro_population_base <= 0 && population > 0) _economy.macro_population_base = population;

	_economy.macro_capital -= _economy.macro_capital / MACRO_CAPITAL_DEPRECIATION;
	if (_economy.macro_capital < 0) _economy.macro_capital = 0;

	UpdateCargoPotential();
}

static constexpr int MACRO_BOND_SLOTS = 48;

/** Pounds at the 1920 price level, then output points. The flow cap keeps one month from dominating demand. */
static int64_t PoundsToPoints(int64_t pounds, int64_t cap)
{
	if (pounds <= 0) return 0;
	const int64_t price_level = std::max<int64_t>(static_cast<int64_t>(_economy.inflation_prices), static_cast<int64_t>(1) << 16);
	const int64_t real_pounds = pounds * (static_cast<int64_t>(1) << 16) / price_level;
	return std::clamp<int64_t>(real_pounds / MACRO_POUNDS_PER_POINT, 0, cap);
}

static void UpdateCargoPotential()
{
	const int64_t baseline = std::max<int32_t>(_settings_game.economy.islm_natural_output, 1);
	const int64_t capacity = std::max<int64_t>(_economy.macro_cargo_capacity, 0);
	/* The first reading keeps today's potential. After that, capacity and potential move by the same percent. */
	if (_economy.macro_cargo_capacity_base <= 0 && capacity > 0) {
		const int64_t natural = _economy.macro_natural_output;
		_economy.macro_cargo_capacity_base = natural > 0 ? std::max<int64_t>(capacity * baseline / natural, 1) : capacity;
	}
	int64_t potential = baseline;
	if (_economy.macro_cargo_capacity_base > 0) {
		potential = baseline * capacity / _economy.macro_cargo_capacity_base;
	}
	_economy.macro_cargo_potential = ClampI32(std::clamp<int64_t>(potential, 100, 10000000), 100, 10000000);
}

void MacroNoteSubsidyPayment(int64_t pounds)
{
	if (pounds <= 0 || !_settings_game.economy.islm_enabled || _game_mode == GameMode::Menu) return;
	_economy.macro_subsidy_pounds += pounds;
	EconomyLabNoteSubsidy(pounds);
}

static void MeasurePrivateDebt()
{
	int64_t loans = 0;
	for (const Company *company : Company::Iterate()) loans += static_cast<int64_t>(company->current_loan);
	const int64_t points = PoundsToPoints(loans, 1000000000);
	if (_economy.macro_history_count == 0) {
		_economy.macro_private_debt_delta = 0;
	} else {
		_economy.macro_private_debt_delta = ClampI32(points - _economy.macro_private_debt, -10000000, 10000000);
	}
	_economy.macro_private_debt = points;
}

static int64_t BondStock()
{
	int64_t stock = 0;
	for (int i = 0; i < _economy.macro_bond_count; i++) stock += _economy.macro_bond_amount[i];
	return stock;
}

static int64_t BondInterest()
{
	int64_t interest = 0;
	for (int i = 0; i < _economy.macro_bond_count; i++) {
		interest += static_cast<int64_t>(_economy.macro_bond_amount[i]) * _economy.macro_bond_coupon[i] / 120000;
	}
	return interest;
}

static void IssueBond(int64_t amount, int32_t coupon_bp)
{
	if (amount <= 0) return;
	coupon_bp = std::clamp<int32_t>(coupon_bp, 0, 10000);
	if (_economy.macro_bond_count < MACRO_BOND_SLOTS) {
		const int slot = _economy.macro_bond_count++;
		_economy.macro_bond_amount[slot] = ClampI32(amount, 1, 1000000000);
		_economy.macro_bond_coupon[slot] = static_cast<int16_t>(coupon_bp);
		return;
	}
	const int64_t old_amount = _economy.macro_bond_amount[0];
	const int64_t total = old_amount + amount;
	const int64_t coupon = total > 0 ? (old_amount * _economy.macro_bond_coupon[0] + amount * coupon_bp) / total : coupon_bp;
	_economy.macro_bond_amount[0] = ClampI32(total, 1, 1000000000);
	_economy.macro_bond_coupon[0] = static_cast<int16_t>(std::clamp<int64_t>(coupon, 0, 10000));
}

static void BuyBackBonds(int64_t budget)
{
	while (budget > 0 && _economy.macro_bond_count > 0) {
		int best = 0;
		for (int i = 1; i < _economy.macro_bond_count; i++) {
			if (_economy.macro_bond_coupon[i] > _economy.macro_bond_coupon[best]) best = i;
		}
		const int64_t amount = _economy.macro_bond_amount[best];
		if (amount <= budget) {
			budget -= amount;
			const int last = _economy.macro_bond_count - 1;
			_economy.macro_bond_amount[best] = _economy.macro_bond_amount[last];
			_economy.macro_bond_coupon[best] = _economy.macro_bond_coupon[last];
			_economy.macro_bond_amount[last] = 0;
			_economy.macro_bond_coupon[last] = 0;
			_economy.macro_bond_count--;
		} else {
			_economy.macro_bond_amount[best] = static_cast<int32_t>(amount - budget);
			budget = 0;
		}
	}
}

/** A deficit issues a bond at this month's market rate. A surplus buys back the dearest coupons first. */
static void UpdatePublicDebt(const IsLmResult &result, int64_t government)
{
	if (!result.ok) return;

	const int64_t interest = BondInterest();
	const int64_t financing = government - result.tax_revenue + interest;
	int64_t issued = 0;
	int64_t bought = 0;
	if (financing > 0) {
		issued = financing;
		IssueBond(financing, std::max<int32_t>(result.interest_bp, 0));
	} else if (financing < 0) {
		const int64_t before = BondStock();
		BuyBackBonds(-financing);
		bought = before - BondStock();
	}
	_economy.macro_debt = BondStock();
	_economy.macro_debt_service = ClampI32(interest, 0, 10000000);
	_economy.macro_bond_issue = ClampI32(issued, 0, 10000000);
	_economy.macro_bond_buyback = ClampI32(bought, 0, 10000000);
	_economy.macro_private_interest = ClampI32(_economy.macro_private_debt * std::max<int32_t>(result.interest_bp, 0) / 120000, 0, 10000000);
}

static void RecordHistory(int32_t public_debt_delta)
{
	if (_economy.macro_history_count < MACRO_HISTORY_MONTHS) {
		_economy.macro_history_count++;
	} else {
		for (int series = 0; series < MACRO_HIST_SERIES; series++) {
			for (int month = 1; month < MACRO_HISTORY_MONTHS; month++) {
				const int index = series * MACRO_HISTORY_MONTHS + month;
				_economy.macro_history[index - 1] = _economy.macro_history[index];
				_economy.macro_history_prices[index - 1] = _economy.macro_history_prices[index];
			}
		}
	}
	const int pos = _economy.macro_history_count - 1;
	const int32_t subsidies = static_cast<int32_t>(_settings_game.economy.islm_subsidies) + _economy.macro_subsidy_game;
	const int32_t values[MACRO_HIST_SERIES] = {
		_economy.macro_inflation_bp,
		public_debt_delta,
		_economy.macro_private_debt_price,
		_economy.macro_output,
		_economy.macro_natural_output,
		_economy.macro_investment,
		_economy.macro_government,
		subsidies,
	};
	const int32_t prices[MACRO_HIST_SERIES] = {
		_economy.macro_price_index / MACRO_PRICE_SCALE,
		_economy.macro_real_money,
		_economy.macro_money_demand,
		_economy.macro_consumption,
		_economy.macro_interest_bp,
		_economy.macro_unemployment_bp,
		_economy.macro_tax_revenue,
		_economy.macro_exogenous_output,
	};
	for (int series = 0; series < MACRO_HIST_SERIES; series++) {
		_economy.macro_history[series * MACRO_HISTORY_MONTHS + pos] = values[series];
		_economy.macro_history_prices[series * MACRO_HISTORY_MONTHS + pos] = prices[series];
	}
}

int MacroHistoryCount()
{
	return _economy.macro_history_count;
}

int32_t MacroHistoryValue(int series, int age)
{
	const int count = _economy.macro_history_count;
	if (series < 0 || series >= MACRO_HIST_SERIES * 2 || age < 0 || age >= count) return 0;
	const std::array<int32_t, 192> &bank = series < MACRO_HIST_SERIES ? _economy.macro_history : _economy.macro_history_prices;
	const int slot = series < MACRO_HIST_SERIES ? series : series - MACRO_HIST_SERIES;
	return bank[slot * MACRO_HISTORY_MONTHS + (count - 1 - age)];
}

static void RecalculateMacroEconomy(bool advance_prices)
{
	if (_economy.macro_price_index <= 0) {
		const int64_t initial = std::max<int32_t>(_settings_game.economy.islm_initial_price_index, 100);
		_economy.macro_price_index = ClampI32(initial * MACRO_PRICE_SCALE, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
		_economy.macro_expected_inflation_bp = _settings_game.economy.islm_initial_expected_inflation * 100;
	}
	if (advance_prices) {
		AdvanceAggregateSupply();
		MeasurePrivateDebt();
	}
	const GameFlows flows = _generating_world ? GameFlows{} : MeasureGameFlows(advance_prices);
	EconomyLabFlows lab_flows;
	lab_flows.capex_pounds = flows.capex;
	lab_flows.opex_pounds = flows.opex;
	lab_flows.government_points = _settings_game.economy.islm_government_spending;
	lab_flows.tax_rate_percent = _settings_game.economy.islm_tax_rate_percent;
	lab_flows.autonomous_tax_points = _settings_game.economy.islm_autonomous_tax;
	lab_flows.population = _economy.macro_population;
	lab_flows.capacity = _economy.macro_cargo_capacity;
	lab_flows.capital_stock = _economy.macro_capital;
	lab_flows.payment_index = static_cast<int64_t>(std::max<uint64_t>(_economy.inflation_payment, static_cast<uint64_t>(PAYMENT_INDEX_BASE)));

	const EconomyLabAccounts accounts = EconomyLabCloseMonth(lab_flows, advance_prices);
	if (advance_prices) _economy.macro_subsidy_pounds = 0;
	if (accounts.reset_legacy_capital) _economy.macro_capital = 0;
	if (accounts.recalibrate) {
		const int32_t restart = _settings_game.economy.islm_initial_expected_inflation * 100;
		_economy.macro_expected_inflation_bp = restart;
		_economy.macro_inflation_bp = restart;
		const int64_t initial = std::max<int32_t>(_settings_game.economy.islm_initial_price_index, 100);
		_economy.macro_price_index = ClampI32(initial * MACRO_PRICE_SCALE, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
		_economy.macro_bond_count = 0;
		_economy.macro_bond_amount.fill(0);
		_economy.macro_bond_coupon.fill(0);
		_economy.macro_debt = 0;
		_economy.macro_debt_service = 0;
	}

	const int64_t debt_before = _economy.macro_debt;
	const IsLmResult result = PublishAccounts(accounts, _economy.macro_price_index);
	const int32_t inflation_bp = result.ok ? PhillipsInflation(_economy.macro_expected_inflation_bp, result.output_gap_bp) : _economy.macro_inflation_bp;
	StoreEquilibrium(result, inflation_bp);
	if (result.ok) {
		_economy.macro_company_investment = result.investment;
		_economy.macro_government = ClampI32(accounts.government, 0, 2000000000);
		_economy.macro_natural_output = ClampI32(accounts.potential, 0, 2000000000);
		_economy.macro_subsidy_game = ClampI32(accounts.subsidies, 0, 2000000000);
		if (advance_prices) {
			_economy.macro_capital = std::min<int64_t>(_economy.macro_capital + accounts.investment, 1000000000000);
		}
	}
	if (result.ok && advance_prices) {
		AdvancePhillipsCurve(inflation_bp);
		UpdatePublicDebt(result, _economy.macro_government);
		int64_t loans = 0;
		for (const Company *company : Company::Iterate()) loans += static_cast<int64_t>(company->current_loan);
		const EconomyLabOpen open = EconomyLabApplyOpenEconomy(accounts, result.interest_bp, _economy.macro_price_index, _economy.macro_debt, loans, _economy.macro_population, result.unemployment_bp);
		_economy.macro_exogenous_output = ClampI32(open.discrepancy, -2000000000, 2000000000);
		const int charged = std::clamp(open.loan_rate_bp / 100, 0, 255);
		_economy.interest_rate = static_cast<uint8_t>(charged);
		RecordHistory(ClampI32(_economy.macro_debt - debt_before, -10000000, 10000000));
	}
	PublishMacroState();
}

static void ClearMacroState()
{
	_economy.macro_price_index = 0;
	_economy.macro_output = 0;
	_economy.macro_interest_bp = 0;
	_economy.macro_inflation_bp = 0;
	_economy.macro_expected_inflation_bp = 0;
	_economy.macro_output_gap_bp = 0;
	_economy.macro_consumption = 0;
	_economy.macro_investment = 0;
	_economy.macro_tax_revenue = 0;
	_economy.macro_real_money = 0;
	_economy.macro_ok = 0;
	_economy.macro_natural_output = 0;
	_economy.macro_unemployment_bp = 0;
	_economy.macro_population_growth_bp = 0;
	_economy.macro_company_investment = 0;
	_economy.macro_debt_service = 0;
	_economy.macro_lagged_inflation_bp = 0;
	_economy.macro_debt = 0;
	_economy.macro_capital = 0;
	_economy.macro_population = 0;
	_economy.macro_population_snapshot = 0;
	_economy.macro_population_base = 0;
	_economy.macro_cargo_capacity = 0;
	_economy.macro_cargo_capacity_base = 0;
	_economy.macro_company_spent_year = 0;
	_economy.macro_cargo_potential = 0;
	_economy.macro_government = 0;
	_economy.macro_game_output = 0;
	_economy.macro_exogenous_output = 0;
	_economy.macro_money_demand = 0;
	_economy.macro_demand_anchor = 0;
	_economy.macro_game_anchor = 0;
	_economy.macro_anchor_set = 0;
	_economy.macro_subsidy_game = 0;
	_economy.macro_bond_issue = 0;
	_economy.macro_bond_buyback = 0;
	_economy.macro_private_debt_delta = 0;
	_economy.macro_private_interest = 0;
	_economy.macro_private_debt = 0;
	_economy.macro_subsidy_pounds = 0;
	_economy.macro_bond_count = 0;
	_economy.macro_history_count = 0;
	_economy.macro_history.fill(0);
	_economy.macro_history_prices.fill(0);
	_economy.macro_bond_amount.fill(0);
	_economy.macro_bond_coupon.fill(0);
}

void ResetMacroEconomy()
{
	EconomyLabReset();
	ClearMacroState();

	if (!_settings_game.economy.islm_enabled) return;

	_economy.macro_natural_output = std::max<int32_t>(_settings_game.economy.islm_natural_output, 1);

	RecalculateMacroEconomy(false);

	/* Prices inherited from 1920 follow the starting expectation. The monthly
	 * Phillips curve takes over once the game is running. */
	const int32_t historical = std::clamp<int32_t>(_settings_game.economy.islm_initial_expected_inflation, 0, 255);
	const int32_t historical_payment = std::clamp(historical * _settings_game.economy.islm_payment_inflation_percent / 100, 0, 255);
	_economy.infl_amount = static_cast<uint8_t>(historical);
	_economy.infl_amount_pr = static_cast<uint8_t>(historical_payment);
}

void RefreshMacroEconomy()
{
	/* Windows and setting changes may call this. Observation does not resolve the month. */
	PublishMacroState();
}

/** Raise one autonomous flow by the annual inflation rate. The result stays inside the setting's range. */
static uint16_t IndexAutonomous(uint16_t value, int32_t inflation_bp)
{
	int64_t next = static_cast<int64_t>(value) + static_cast<int64_t>(value) * inflation_bp / 10000;
	if (next < 0) next = 0;
	if (next > 20000) next = 20000;
	return static_cast<uint16_t>(next);
}

/**
 * Once a year, autonomous consumption, government purchases, autonomous investment and
 * autonomous taxes grow with the inflation rate. The demand anchor moves by the same
 * amount, so this indexation keeps their real weight and is not a new fiscal impulse.
 * The money supply and the tax rate are not indexed: those are the policy instruments.
 */
static void IndexAutonomousFlows()
{
	if (_economy.macro_ok == 0 || _economy.macro_anchor_set == 0) return;

	const int32_t inflation_bp = _economy.macro_inflation_bp;
	if (inflation_bp == 0) return;

	EconomySettings &cfg = _settings_game.economy;
	const int64_t mpc = cfg.islm_mpc_percent;
	const int64_t before = static_cast<int64_t>(cfg.islm_autonomous_consumption) + cfg.islm_government_spending - mpc * cfg.islm_autonomous_tax / 100;

	cfg.islm_autonomous_consumption = IndexAutonomous(cfg.islm_autonomous_consumption, inflation_bp);
	cfg.islm_government_spending = IndexAutonomous(cfg.islm_government_spending, inflation_bp);
	cfg.islm_autonomous_investment = IndexAutonomous(cfg.islm_autonomous_investment, inflation_bp);
	cfg.islm_autonomous_tax = IndexAutonomous(cfg.islm_autonomous_tax, inflation_bp);

	const int64_t after = static_cast<int64_t>(cfg.islm_autonomous_consumption) + cfg.islm_government_spending - mpc * cfg.islm_autonomous_tax / 100;
	_economy.macro_demand_anchor = ClampI32(static_cast<int64_t>(_economy.macro_demand_anchor) + after - before, -10000000, 10000000);
}

void MacroEconomyMonthly()
{
	if (_game_mode == GameMode::Menu || !_settings_game.economy.islm_enabled) return;

	/* January uses last year's inflation. The solve below then publishes the new levels. */
	if (!_generating_world && TimerGameEconomy::month == 0) IndexAutonomousFlows();

	RecalculateMacroEconomy(true);
	if (_economy.macro_ok == 0) return;

	RecomputePrices();

	if (!_generating_world && TimerGameEconomy::month == 0) {
		AddNewsItem(GetEncodedString(STR_NEWS_MACRO_REPORT,
				RoundHundredthsToPercent(_economy.macro_interest_bp),
				RoundHundredthsToPercent(_economy.macro_inflation_bp),
				RoundHundredthsToPercent(_economy.macro_expected_inflation_bp)),
			NewsType::Economy, NewsStyle::Normal, {});
	}
}

uint MacroActivityPermyriad()
{
	if (_game_mode == GameMode::Menu || !_settings_game.economy.islm_enabled || _economy.macro_ok == 0) return 10000;

	const int64_t link = _settings_game.economy.islm_output_gap_cargo_link;
	const int64_t permyriad = 10000 + static_cast<int64_t>(_economy.macro_output_gap_bp) * link / 100;
	return static_cast<uint>(std::clamp<int64_t>(permyriad, 5000, 20000));
}

uint MacroTownIncentivePermyriad(bool growing, uint16_t growth_rate)
{
	if (!growing) return 10000;
	/* A smaller counter means the town builds the next house sooner. */
	if (growth_rate == 0 || growth_rate >= 4000) return 13000;
	const int boost = 13000 + static_cast<int>(4000 - growth_rate) * 7000 / 4000;
	return static_cast<uint>(std::clamp(boost, 13000, 20000));
}

uint MacroIndustryIncentivePermyriad(int production_level)
{
	constexpr int born = 16;
	if (production_level <= born) return 10000;
	const int extra = (production_level - born) * 10000 / born;
	return static_cast<uint>(std::clamp(10000 + extra, 10000, 20000));
}

int32_t MacroOutputAtInterest(int32_t interest_bp)
{
	const int64_t potential = std::max<int32_t>(_economy.macro_natural_output, 1);
	const int64_t neutral = TaylorNeutralBp(_economy.macro_inflation_bp);
	constexpr int64_t phi_y = 50;
	const int64_t gap_bp = (static_cast<int64_t>(interest_bp) - neutral) * 100 / phi_y;
	return ClampI32(potential * (10000 + gap_bp) / 10000, 0, 2000000000);
}

int32_t MacroInterestAtOutput(int32_t output)
{
	const int64_t potential = std::max<int32_t>(_economy.macro_natural_output, 1);
	const int32_t gap_bp = ClampI32((static_cast<int64_t>(output) - potential) * 10000 / potential, -10000, 10000);
	return TaylorRateBp(_economy.macro_inflation_bp, gap_bp);
}

int32_t MacroOutputAtPrice(int32_t)
{
	/* Measured GDP is already realised this month. The price index does not rewrite it. */
	return _economy.macro_output;
}

int32_t MacroPriceAtOutput(int32_t output)
{
	const int64_t natural = std::max<int32_t>(_economy.macro_natural_output, 1);
	const int64_t slope = _settings_game.economy.islm_phillips_slope_percent;
	const int64_t price = static_cast<int64_t>(_economy.macro_price_index) + static_cast<int64_t>(_economy.macro_price_index) * slope * (static_cast<int64_t>(output) - _economy.macro_output) / natural / 100;
	return ClampI32(price, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
}

const char *MacroSeriesName(int series)
{
	static const char *names[] = {
		"Inflación",
		"Cambio de la deuda pública",
		"Precio de la deuda privada",
		"PIB (valor agregado)",
		"Producto potencial",
		"Inversión (formación de capital)",
		"Gasto público",
		"Subsidios (transferencia)",
		"Índice de precios",
		"Dinero real",
		"Demanda de dinero",
		"Consumo",
		"Tasa de interés",
		"Desempleo",
		"Recaudación",
		"Discrepancia del PIB",
	};
	if (series < 0 || series >= static_cast<int>(std::size(names))) return "";
	return names[series];
}

int FillMacroMonitor(MacroMonitorRow *rows, int capacity)
{
	if (rows == nullptr || capacity <= 0) return 0;
	const EconomyLabOpen &open = EconomyLabOpenState();
	const EconomySettings &cfg = _settings_game.economy;
	const MacroMonitorRow table[] = {
		{"PIB producción", "libras de 1920 · flujo · mes · E-006 · valor agregado del transporte", _economy.macro_output, 0},
		{"PIB potencial", "libras de 1920 · stock · mes · E-027 · calibrado una vez", _economy.macro_natural_output, 0},
		{"Brecha", "tasa · stock · mes · (Y-Y*)/Y* · cuentas", _economy.macro_output_gap_bp, 1},
		{"Discrepancia", "libras de 1920 · flujo · mes · E-008 · producción menos gasto", _economy.macro_exogenous_output, 0},
		{"Consumo", "libras de 1920 · flujo · mes · E-007 · pasajeros y correo", _economy.macro_consumption, 0},
		{"Inversión", "libras de 1920 · flujo · mes · E-007 · construcción y vehículos", _economy.macro_investment, 0},
		{"Compras públicas", "libras de 1920 · flujo · mes · E-007 · 1000 puntos = PIB calibrado", _economy.macro_government, 0},
		{"Impuestos", "libras de 1920 · flujo · mes · E-030 · tipo y impuesto autónomo", _economy.macro_tax_revenue, 0},
		{"Subsidios", "libras de 1920 · flujo · mes · E-006 · transferencia, no es consumo público", _economy.macro_subsidy_game, 0},
		{"Exportaciones", "libras de 1920 · flujo · mes · E-044 · tipo de cambio real", open.exports, 0},
		{"Importaciones", "libras de 1920 · flujo · mes · E-045 · actividad y tipo de cambio", open.imports, 0},
		{"Volumen físico", "piezas · flujo · mes · sin ecuación de valor · no es PIB", _economy.macro_game_output, 0},
		{"Tasa de política", "tasa anual · stock · mes · E-032 · Taylor", _economy.macro_interest_bp, 1},
		{"Tasa de préstamo", "tasa anual · stock · mes · E-038 · política más prima", open.loan_rate_bp, 1},
		{"Tasa mundial", "tasa anual · exógena · mes · E-051 · 2%", open.world_rate_bp, 1},
		{"Prima de riesgo", "tasa anual · stock · mes · E-049 · deuda sobre PIB", open.risk_bp, 1},
		{"Inflación", "tasa anual · flujo · mes · Phillips · brecha y expectativas", _economy.macro_inflation_bp, 1},
		{"Inflación esperada", "tasa anual · stock · mes · expectativas adaptativas", _economy.macro_expected_inflation_bp, 1},
		{"Desempleo", "tasa · stock · mes · puente de Okun hasta E-022", _economy.macro_unemployment_bp, 1},
		{"Población", "personas · stock · mes · pueblos", _economy.macro_population, 0},
		{"Fuerza de trabajo", "personas · stock · mes · E-020 · 45% de la población", open.labour_force, 0},
		{"Empleo", "personas · stock · mes · E-022 · fuerza por uno menos desempleo", open.employment, 0},
		{"Tipo de cambio", "índice · stock · mes · E-053 · 100 es el inicio", open.exchange_pm, 2},
		{"Cambio fundamental", "índice · stock · mes · E-052 · paridad de largo plazo", open.fundamental_pm, 2},
		{"Overshooting", "indicador · stock · mes · E-053 · 1 si el spot supera al fundamental", open.overshooting ? 1 : 0, 0},
		{"Deuda pública", "libras de 1920 · stock · mes · E-031 · bonos del Tesoro", _economy.macro_debt, 0},
		{"Interés de la deuda", "libras de 1920 · flujo · mes · cupón pactado", _economy.macro_debt_service, 0},
		{"Préstamos bancarios", "libras de 1920 · stock · mes · E-035 · deuda de las compañías", open.bank_loans, 0},
		{"Depósitos", "libras de 1920 · stock · mes · E-034 · ahorro acumulado", open.bank_deposits, 0},
		{"Reservas bancarias", "libras de 1920 · stock · mes · E-033 · 10% de los depósitos", open.bank_reserves, 0},
		{"Bonos en bancos", "libras de 1920 · stock · mes · E-035 · tenencia del Tesoro", open.bank_bonds, 0},
		{"Capital bancario", "libras de 1920 · stock · mes · E-035 · activo menos depósitos", open.bank_equity, 0},
		{"Hueco bancario", "libras de 1920 · stock · mes · E-035 · debe ser cero", open.bank_gap, 0},
		{"Índice de precios", "índice · stock · mes · Phillips · 1000 es la base", _economy.macro_price_index / 100, 0},
		{"Dinero nominal", "puntos de política · exógeno · mes · aún no es la base monetaria", cfg.islm_money_supply, 0},
		{"Capacidad de carga", "plazas · stock · mes · vehículos · no es PIB", _economy.macro_cargo_capacity, 0},
		{"Capital", "libras de 1920 · stock · mes · E-018 · formación menos depreciación", _economy.macro_capital, 0},
		{"Fidelidad", "indicador · stock · corrida · F0 mide el juego · F2 no promete 100000x", 0, 0},
		{"Decisor", "indicador · stock · mes · regla determinista · el LLM no corre en el tick", 0, 0},
	};
	const int available = static_cast<int>(std::size(table));
	const int count = std::min(available, capacity);
	for (int i = 0; i < count; i++) rows[i] = table[i];
	return count;
}
