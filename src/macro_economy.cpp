/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy.cpp Closed-economy IS-LM equilibrium and expectations-augmented Phillips curve. */

#include "stdafx.h"

#include "macro_economy.h"
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
 *
 * Short run, Keynesian and Patinkin. Prices are given for the month, so output
 * is whatever demand the goods and money markets will support:
 *   Y = C + I + G
 *   T = T0 + t Y
 *   C = C0 + c (Y - T) + real-balance gap + lagged inflation + debt interest received
 *   I = I0 + company investment - b r
 *   M / P = k Y - h r
 * Company investment is what companies spent this month on construction and new
 * vehicles, converted from pounds into output points. Real balances M/P enter
 * consumption, so a higher price level makes households poorer and they spend
 * less. Inflation that has already happened makes them spend sooner. The
 * interest rate still cuts investment, and the loan rate charged in the game
 * is this same rate, so dearer credit means less building next month.
 *
 * Long run, neoclassical. Potential output Y* is not a fixed setting. It starts
 * at the baseline and each month crawls toward
 *   baseline * population / population0 * cargo capacity / capacity0 * capital factor.
 * Population is the towns. Cargo capacity is the vehicles. The capital factor
 * grows with cumulated company investment and shrinks with depreciation.
 * The Phillips curve, pi = pi_e + alpha (Y - Y*) / Y*, then moves prices until
 * the output gap closes on that growing potential. Unemployment is Okun's
 * reading of the same gap, and the gap scales town and industry production.
 */

static constexpr int64_t MACRO_PRICE_SCALE = 100; ///< Stored price index is the displayed index times this.
static constexpr int64_t MACRO_MIN_PRICE = 100 * MACRO_PRICE_SCALE;
static constexpr int64_t MACRO_MAX_PRICE = 200000 * MACRO_PRICE_SCALE;
static constexpr int64_t MACRO_MIN_LEAKAGE = 100; ///< s * 10000, at least one percent of income leaks out of the goods market.
static constexpr int64_t MACRO_POUNDS_PER_POINT = 50000; ///< 1920 pounds of company spending that count as one output point.
static constexpr int64_t MACRO_REAL_BALANCE_EFFECT = 25; ///< Consumption points added per 100 points of extra real money.
static constexpr int64_t MACRO_MAX_SHIFT = 5000;
static constexpr int64_t MACRO_CAPITAL_PRODUCTIVITY = 2; ///< Permyriad of potential output added by one point of capital.
static constexpr int64_t MACRO_CAPITAL_DEPRECIATION = 600; ///< Months to depreciate one part in this many of the capital stock, about 2 percent a year.
static constexpr int64_t MACRO_NATURAL_ADJUSTMENT = 24; ///< Months over which potential output closes on its target.
static constexpr int64_t MACRO_OKUN_NATURAL_BP = 500; ///< Unemployment at a zero output gap, 5 percent.

/** Demand terms known before this month's equilibrium is solved. */
struct PredeterminedDemand {
	int64_t cargo = 0; ///< Delivered cargo units. This is the product.
	int64_t revenue_pounds = 0; ///< Company income. It is what the investment produced.
	int64_t invest_pounds = 0; ///< Construction, new vehicles and infrastructure operating costs.
	int64_t government = 0; ///< Policy spending in its own points, scaled to cargo inside the solve.
	int32_t natural_output = 0;
};

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

/**
 * Solve the IS-LM crossing at the current macro price index.
 * The price index and the game's spending are given, so this is the short-run
 * sticky-price equilibrium. Company investment and the consumption shifts are
 * folded into autonomous demand, which keeps the closed form.
 */
static IsLmResult SolveIsLm(int32_t price_index, const PredeterminedDemand &demand)
{
	const EconomySettings &cfg = _settings_game.economy;
	IsLmResult result;

	const int64_t tax_rate = cfg.islm_tax_rate_percent;
	const int64_t mpc = cfg.islm_mpc_percent;
	const int64_t leakage = 10000 - mpc * (100 - tax_rate);
	const int64_t money_interest = cfg.islm_money_interest_sensitivity;
	const int64_t money_income = cfg.islm_money_income_percent;
	const int64_t natural_output = std::max<int32_t>(demand.natural_output > 0 ? demand.natural_output : cfg.islm_natural_output, 1);
	if (leakage < MACRO_MIN_LEAKAGE || money_interest < 1 || money_income < 1) return result;

	/* Displayed index 1000 is stored as 100000, so real balances are M * 1000 / displayed index. */
	const int64_t price = std::clamp<int64_t>(price_index, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
	const int64_t real_money = static_cast<int64_t>(cfg.islm_money_supply) * 1000 * MACRO_PRICE_SCALE / price;

	/* The product is the cargo the companies delivered. Nothing else invents it. */
	int64_t output = demand.cargo;
	if (output < 1 && _economy.macro_output > 0) output = _economy.macro_output;
	/* A potential still expressed in the old index is brought onto the cargo the network actually delivers. */
	if (output >= 100 && (natural_output > output * 4 || output > natural_output * 4)) {
		_economy.macro_natural_output = ClampI32(output, 100, 10000000);
	}
	const int64_t potential = std::max<int32_t>(_economy.macro_natural_output > 0 ? _economy.macro_natural_output : static_cast<int32_t>(natural_output), 1);

	/* 1000 policy points are one whole product, so a setting of 180 stays about 18 percent of the cargo. */
	int64_t government = output > 0 ? demand.government * output / 1000 : 0;
	if (government < 0) government = 0;

	/* Investment produces the companies' income. Its size is that spending relative to the income it produced. */
	int64_t investment = 0;
	if (output > 0 && demand.invest_pounds > 0) {
		if (demand.revenue_pounds > 0) {
			investment = output * demand.invest_pounds / (demand.revenue_pounds + demand.invest_pounds);
		} else {
			investment = std::max<int64_t>(output / 5, 1);
		}
		if (investment > output * 45 / 100) investment = output * 45 / 100;
		if (investment < 1) investment = 1;
	}

	/* Money demand at a zero rate is k/10 percent of output. k = 50 means 5 percent, the same order as real balances. */
	auto interest_at = [&](int64_t level) {
		if (level < 1) level = 1;
		const int64_t demand_pm = money_income * 10;
		const int64_t supply_pm = real_money * 10000 / level;
		return (demand_pm - supply_pm) * 100 / money_interest;
	};
	const int64_t tax_revenue = static_cast<int64_t>(cfg.islm_autonomous_tax) * output / 1000 + tax_rate * output / 100;
	/* A surplus lends to the companies: the interest rate falls and the price of their debt rises. */
	const int64_t surplus = tax_revenue - government;
	int64_t interest_bp = output > 0 ? interest_at(output) - surplus * 5000 / output : interest_at(potential);
	if (investment > 0) {
		const int64_t rate_percent = std::max<int64_t>(interest_bp, 0) / 100;
		const int64_t kept = investment * 1000 / (1000 + cfg.islm_investment_sensitivity * rate_percent);
		investment = std::max<int64_t>(kept, 1);
	}
	if (output > 0 && investment > output) investment = output;
	if (output > 0 && government > output - investment) government = output - investment;
	/* No investment, no income, no consumption. Otherwise consumption is what the identity leaves. */
	const int64_t consumption = investment > 0 ? output - investment - government : 0;
	const int64_t yield_bp = std::max<int64_t>(interest_bp, 50);
	const int64_t debt_price = 500 * 1000 / yield_bp;

	result.ok = output > 0;
	result.leakage = static_cast<int32_t>(leakage);
	result.output = ClampI32(output, 0, 10000000);
	result.interest_bp = ClampI32(interest_bp, -5000, 10000);
	result.consumption = ClampI32(consumption, 0, 10000000);
	result.investment = ClampI32(investment, 0, 10000000);
	result.tax_revenue = ClampI32(tax_revenue, 0, 10000000);
	result.real_money = ClampI32(real_money, 0, 10000000);
	result.money_demand = ClampI32(money_income * output / 1000, 0, 10000000);
	result.game_output = result.output;
	result.exogenous_output = 0;
	result.debt_price = ClampI32(debt_price, 1, 10000000);
	result.output_gap_bp = ClampI32((output - potential) * 10000 / potential, -10000, 100000);
	/* Okun: a one-point boom cuts unemployment by half a point. */
	result.unemployment_bp = ClampI32(MACRO_OKUN_NATURAL_BP - result.output_gap_bp / 2, 0, 5000);
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

/** Company income this economy year. Revenue is stored as a negative cost. */
static int64_t CompanyRevenueBook()
{
	int64_t earned = 0;
	for (const Company *company : Company::Iterate()) {
		earned -= static_cast<int64_t>(company->yearly_expenses[0][ExpensesType::TrainRevenue]);
		earned -= static_cast<int64_t>(company->yearly_expenses[0][ExpensesType::RoadVehRevenue]);
		earned -= static_cast<int64_t>(company->yearly_expenses[0][ExpensesType::AircraftRevenue]);
		earned -= static_cast<int64_t>(company->yearly_expenses[0][ExpensesType::ShipRevenue]);
	}
	return earned;
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

static int64_t cargo_delivered_open = 0;
static int64_t cargo_delivered_closed = 0;

void MacroNoteCargoDelivery(int64_t units)
{
	if (units > 0) cargo_delivered_open += units;
}

/** Cargo already delivered this quarter, used until the first month of the new build has its own count. */
static int64_t SeedDeliveredCargo()
{
	int64_t total = 0;
	for (const Company *company : Company::Iterate()) {
		total += static_cast<int64_t>(company->cur_economy.delivered_cargo.GetSum<uint64_t>());
	}
	const int months = static_cast<int>(TimerGameEconomy::month) % 3 + 1;
	return std::max<int64_t>(total / months, total > 0 ? 1 : 0);
}

static int64_t DeliveredCargo(bool close_month)
{
	if (close_month) {
		if (cargo_delivered_open > 0) cargo_delivered_closed = cargo_delivered_open;
		cargo_delivered_open = 0;
		if (cargo_delivered_closed > 0) return cargo_delivered_closed;
		return SeedDeliveredCargo();
	}
	const int64_t base = cargo_delivered_closed > 0 ? cargo_delivered_closed : SeedDeliveredCargo();
	return std::max(cargo_delivered_open, base);
}

struct GameFlows {
	int64_t cargo = 0;
	int64_t revenue = 0;
	int64_t invest = 0;
};

/** Cargo delivered, the income it produced, and the spending that produced it. */
static GameFlows MeasureGameFlows(bool close_month)
{
	GameFlows flows;
	flows.cargo = DeliveredCargo(close_month);

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

	static int64_t revenue_mark = 0;
	const int64_t revenue = CompanyRevenueBook();
	int64_t revenue_flow = revenue - revenue_mark;
	if (revenue_flow < 0) revenue_flow = std::max<int64_t>(revenue, 0);
	if (close_month) revenue_mark = revenue;

	flows.revenue = std::max<int64_t>(revenue_flow, 0);
	flows.invest = std::max<int64_t>(capex_flow, 0) + InfrastructureOperatingCost();
	return flows;
}

static void UpdateCargoPotential();

/** Refresh the population and cargo capacity the window shows. Does not move potential output. */
void MacroEconomyObserve()
{
	if (_generating_world || _game_mode == GameMode::Menu || !_settings_game.economy.islm_enabled) return;

	_economy.macro_population = TownPopulation();
	_economy.macro_cargo_capacity = InstalledCargoCapacity();
	if (_economy.macro_population_base <= 0 && _economy.macro_population > 0) _economy.macro_population_base = _economy.macro_population;
	UpdateCargoPotential();
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
	const int64_t baseline = std::max<int64_t>(_economy.macro_cargo_potential, 1);
	const int64_t population_factor = _economy.macro_population_base > 0 ? std::clamp<int64_t>(population * 10000 / _economy.macro_population_base, 2500, 40000) : 10000;
	const int64_t capital_factor = std::clamp<int64_t>(10000 + _economy.macro_capital * MACRO_CAPITAL_PRODUCTIVITY, 10000, 40000);
	int64_t target = baseline * population_factor / 10000;
	target = target * capital_factor / 10000;
	target = std::clamp<int64_t>(target, 100, 10000000);

	int64_t natural = _economy.macro_natural_output > 0 ? _economy.macro_natural_output : baseline;
	const int64_t gap = target - natural;
	natural += gap / MACRO_NATURAL_ADJUSTMENT;
	if (gap != 0 && gap / MACRO_NATURAL_ADJUSTMENT == 0) natural += gap > 0 ? 1 : -1;
	_economy.macro_natural_output = ClampI32(natural, 100, 10000000);
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

static int64_t EffectiveGovernment()
{
	return static_cast<int64_t>(_settings_game.economy.islm_government_spending) + _settings_game.economy.islm_subsidies + _economy.macro_subsidy_game;
}

void MacroNoteSubsidyPayment(int64_t pounds)
{
	if (pounds <= 0 || !_settings_game.economy.islm_enabled || _game_mode == GameMode::Menu) return;
	_economy.macro_subsidy_pounds += pounds;
}

/** Turn this month's route-subsidy payments into output points and start a new month. */
static void CloseSubsidyMonth()
{
	_economy.macro_subsidy_game = static_cast<int32_t>(PoundsToPoints(_economy.macro_subsidy_pounds, MACRO_MAX_SHIFT));
	_economy.macro_subsidy_pounds = 0;
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
	if (_economy.macro_natural_output <= 0) {
		_economy.macro_natural_output = std::max<int32_t>(_settings_game.economy.islm_natural_output, 1);
	}
	if (advance_prices) {
		CloseSubsidyMonth();
		AdvanceAggregateSupply();
		MeasurePrivateDebt();
	}
	const GameFlows flows = _generating_world ? GameFlows{} : MeasureGameFlows(advance_prices);
	_economy.macro_government = ClampI32(EffectiveGovernment(), 0, 10000000);

	PredeterminedDemand demand;
	demand.cargo = flows.cargo;
	demand.revenue_pounds = flows.revenue;
	demand.invest_pounds = flows.invest;
	demand.government = _economy.macro_government;
	demand.natural_output = _economy.macro_natural_output;

	const int64_t debt_before = _economy.macro_debt;
	const IsLmResult result = SolveIsLm(_economy.macro_price_index, demand);
	const int32_t inflation_bp = result.ok ? PhillipsInflation(_economy.macro_expected_inflation_bp, result.output_gap_bp) : _economy.macro_inflation_bp;
	StoreEquilibrium(result, inflation_bp);
	if (result.ok) {
		_economy.macro_company_investment = result.investment;
		_economy.macro_government = result.output - result.consumption - result.investment;
		if (advance_prices) {
			_economy.macro_capital = std::min<int64_t>(_economy.macro_capital + result.investment, 1000000);
		}
	}
	if (result.ok && advance_prices) {
		AdvancePhillipsCurve(inflation_bp);
		UpdatePublicDebt(result, _economy.macro_government);
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
	if (_game_mode == GameMode::Menu) return;
	if (!_settings_game.economy.islm_enabled) {
		PublishMacroState();
		return;
	}
	RecalculateMacroEconomy(false);
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
	const int64_t output_now = std::max<int32_t>(_economy.macro_output, 1);
	/* Ten percentage points of interest move output by about a quarter, so the IS curve is visible. */
	const int64_t slope = std::max<int64_t>(output_now * std::max<int>(_settings_game.economy.islm_investment_sensitivity, 1) / 80000, 1);
	const int64_t output = output_now + (static_cast<int64_t>(_economy.macro_interest_bp) - interest_bp) * slope;
	return ClampI32(output, 0, 10000000);
}

int32_t MacroInterestAtOutput(int32_t output)
{
	const int64_t sensitivity = std::max<int>(_settings_game.economy.islm_money_interest_sensitivity, 1);
	const int64_t income = _settings_game.economy.islm_money_income_percent;
	const int64_t level = std::max<int32_t>(output, 1);
	const int64_t demand_pm = income * 10;
	const int64_t supply_pm = static_cast<int64_t>(_economy.macro_real_money) * 10000 / level;
	int64_t interest = (demand_pm - supply_pm) * 100 / sensitivity;
	const int64_t base = std::max<int32_t>(_economy.macro_output, 1);
	const int64_t surplus = static_cast<int64_t>(_economy.macro_tax_revenue) - _economy.macro_government;
	interest -= surplus * 5000 / base;
	return ClampI32(interest, -5000, 10000);
}

int32_t MacroOutputAtPrice(int32_t price_index)
{
	const EconomySettings &cfg = _settings_game.economy;
	auto balance_at = [&](int64_t price) {
		price = std::clamp<int64_t>(price, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
		const int64_t real_money = static_cast<int64_t>(cfg.islm_money_supply) * 1000 * MACRO_PRICE_SCALE / price;
		return std::clamp<int64_t>((real_money - static_cast<int64_t>(cfg.islm_money_supply)) * MACRO_REAL_BALANCE_EFFECT / 100, -MACRO_MAX_SHIFT, MACRO_MAX_SHIFT);
	};
	const int64_t y = std::max<int32_t>(_economy.macro_output, 1);
	const int64_t output = y + (balance_at(price_index) - balance_at(_economy.macro_price_index)) * y / 20000;
	return ClampI32(output, 0, 10000000);
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
		"Producto (carga entregada)",
		"Producto potencial",
		"Inversión",
		"Gasto público",
		"Subsidios",
		"Índice de precios",
		"Dinero real",
		"Demanda de dinero",
		"Consumo",
		"Tasa de interés",
		"Desempleo",
		"Recaudación",
		"Impulso exógeno",
	};
	if (series < 0 || series >= static_cast<int>(std::size(names))) return "";
	return names[series];
}
