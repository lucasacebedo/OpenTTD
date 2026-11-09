/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file economy_type.h Types related to the economy. */

#ifndef ECONOMY_TYPE_H
#define ECONOMY_TYPE_H

#include "core/overflowsafe_type.hpp"
#include "core/enum_type.hpp"
#include "core/pool_type.hpp"

#include <array>

typedef OverflowSafeInt64 Money;

/** Type of the game economy. */
enum class EconomyType : uint8_t {
	Begin, ///< The lowest valid value.
	Original = EconomyType::Begin, ///< Imitates original TTD economy.
	Smooth, ///< Makes production changes more often, and in smaller steps.
	Frozen, ///< Stops production changes and industry closures.
	End, ///< Economy type end marker.
};

/**
 * Minimum allowed value of town_cargo_scale/industry_cargo_scale.
 * Below 13, callback-based industries would produce less than once per month. We round up to 15% because it's a nicer number.
 * Towns use the same minimum to match, and because below this small towns often produce no cargo.
 */
static const int MIN_CARGO_SCALE = 15;
/**
 * Maximum allowed value of town_cargo_scale/industry_cargo_scale.
 * Above 340, callback-based industries would produce more than once per day, which GRFs do not expect.
 * Towns use the same maximum to match.
 */
static const int MAX_CARGO_SCALE = 300;
/** Default value of town_cargo_scale/industry_cargo_scale. */
static const int DEF_CARGO_SCALE = 100;

/** Data of the economy. */
struct Economy {
	Money max_loan;                       ///< NOSAVE: Maximum possible loan
	int16_t fluct;                          ///< Economy fluctuation status
	uint8_t interest_rate;                   ///< Interest
	uint8_t infl_amount;                     ///< inflation amount
	uint8_t infl_amount_pr;                  ///< inflation rate for payment rates
	uint32_t industry_daily_change_counter; ///< Bits 31-16 are number of industry to be performed, 15-0 are fractional collected daily
	uint32_t industry_daily_increment;      ///< The value which will increment industry_daily_change_counter. Computed value. NOSAVE
	uint64_t inflation_prices;              ///< Cumulated inflation of prices since game start; 16 bit fractional part
	uint64_t inflation_payment;             ///< Cumulated inflation of cargo payment since game start; 16 bit fractional part

	/* IS-LM equilibrium and expectations-augmented Phillips curve.
	 * Rates suffixed _bp are hundredths of a percent: 250 means 2.50%.
	 * Flow variables are index points, the same unit as natural output. */
	int32_t macro_output;                   ///< Equilibrium real output, Y
	int32_t macro_interest_bp;              ///< Equilibrium nominal interest, hundredths of a percent
	int32_t macro_inflation_bp;             ///< Phillips-curve inflation, hundredths of a percent per year
	int32_t macro_expected_inflation_bp;    ///< Adaptive inflation expectation, hundredths of a percent per year
	int32_t macro_price_index;              ///< Macro price index times 100. 100000 is the base of 1000.00 used to deflate money
	int32_t macro_output_gap_bp;            ///< (Y - Y*) / Y* in hundredths of a percent
	int32_t macro_consumption;              ///< Equilibrium consumption
	int32_t macro_investment;               ///< Equilibrium investment
	int32_t macro_tax_revenue;              ///< Equilibrium tax revenue
	int32_t macro_real_money;               ///< Real money balances, M / P
	uint8_t macro_ok;                       ///< 1 when the last IS-LM solve found an equilibrium
	int32_t macro_natural_output;           ///< Living potential output. It starts at the baseline and moves with population, cargo capacity and capital
	int32_t macro_unemployment_bp;          ///< Okun unemployment, hundredths of a percent
	int32_t macro_population_growth_bp;     ///< Town population growth over the last economy month, hundredths of a percent
	int32_t macro_company_investment;       ///< This month's company construction and vehicle purchases, in output points
	int32_t macro_debt_service;             ///< Interest the treasury pays on the public debt this month
	int32_t macro_lagged_inflation_bp;      ///< Inflation already realised, the rate that pushes households to spend
	int64_t macro_debt;                     ///< Public debt, in output points. A surplus pays it down
	int64_t macro_capital;                  ///< Cumulated real company investment, after depreciation
	int64_t macro_population;               ///< Town population last observed
	int64_t macro_population_snapshot;      ///< Town population at the previous economy month, used for the growth rate
	int64_t macro_population_base;          ///< Town population when the living potential output was anchored
	int64_t macro_cargo_capacity;           ///< Installed vehicle cargo capacity
	int64_t macro_cargo_capacity_base;      ///< Cargo capacity when the living potential output was anchored
	int64_t macro_company_spent_year;       ///< Construction and new-vehicle spending booked so far this economy year, in pounds
	int32_t macro_cargo_potential;          ///< Potential output implied by installed cargo capacity
	int32_t macro_government;               ///< Government spending that enters demand: purchases + subsidies + route-subsidy payments
	int32_t macro_game_output;              ///< Part of output that comes from the game: potential, company investment and route subsidies
	int32_t macro_exogenous_output;         ///< Part of output that moved because an exogenous policy variable changed
	int32_t macro_money_demand;             ///< Money demanded at a zero interest rate, kY
	int32_t macro_demand_anchor;            ///< Autonomous demand when output was anchored to the game
	int32_t macro_game_anchor;              ///< Game part of autonomous demand at that same moment
	uint8_t macro_anchor_set;               ///< 1 once output has been anchored to the game's potential
	int32_t macro_subsidy_game;             ///< Route-subsidy payments of the last closed month, in output points
	int32_t macro_bond_issue;               ///< Bonds issued this month
	int32_t macro_bond_buyback;             ///< Bonds bought back this month
	int32_t macro_private_debt_delta;       ///< Change in private debt this month
	int32_t macro_private_interest;         ///< Interest companies owe on their loans this month, in output points
	int32_t macro_private_debt_price;       ///< Price of private debt. It rises when the interest rate falls
	int64_t macro_private_debt;             ///< Company loans, deflated to output points
	int64_t macro_subsidy_pounds;           ///< Route-subsidy payments booked so far this month, in pounds
	uint8_t macro_bond_count;               ///< Cohorts in the public bond book
	uint8_t macro_history_count;            ///< Months stored for the macro graphs, up to 24
	std::array<int32_t, 192> macro_history{}; ///< Eight series of 24 months: inflation, public-debt change, private-debt change, output, cargo potential, investment, government, subsidies
	std::array<int32_t, 192> macro_history_prices{}; ///< Eight more series: price, real money, money demand, consumption, interest, unemployment, taxes, exogenous impulse
	std::array<int32_t, 48> macro_bond_amount{}; ///< Face value of each bond cohort
	std::array<int16_t, 48> macro_bond_coupon{}; ///< Coupon of each cohort, hundredths of a percent per year

	/* Old stuff for savegame conversion only */
	Money old_max_loan_unround;           ///< Old: Unrounded max loan
	uint16_t old_max_loan_unround_fract;    ///< Old: Fraction of the unrounded max loan
};

/** Score categories in the detailed performance rating. */
enum class ScoreID : uint8_t {
	Begin, ///< The lowest valid value.
	Vehicles = ScoreID::Begin, ///< Number of vehicles that turned profit last year.
	Stations, ///< Number of recently-serviced stations.
	MinProfit, ///< The profit of the vehicle with the lowest income.
	MinIncome, ///< Income in the quarter with the lowest profit of the last 12 quarters.
	MaxIncome, ///< Income in the quarter with the highest profit of the last 12 quarters.
	Delivered, ///< Units of cargo delivered in the last four quarter.
	Cargo, ///< Number of types of cargo delivered in the last four quarter.
	Money, ///< Amount of money company has in the bank.
	Loan, ///< The amount of money company can take as a loan.
	Total, ///< Total points out of possible points ,must always be the last entry.
	End, ///< Score ID end marker.
};

/**
 * The max score that can be in the performance history.
 * The scores together of score_info is allowed to be more!
 */
static constexpr int SCORE_MAX = 1000;

/** Data structure for storing how the score is computed for a single score id. */
struct ScoreInfo {
	int score;  ///< How much score it will give
	int needed; ///< How much you need to get the perfect score
};

/**
 * Enumeration of all base prices for use with #Prices.
 * The prices are ordered as they are expected by NewGRF cost multipliers, so don't shuffle them.
 */
enum class Price : uint8_t {
	Begin, ///< The lowest valid value.
	StationValue = Price::Begin, ///< Stations value and additional constant company running fee.
	BuildRail, ///< Price for building rails.
	BuildRoad, ///< Price for building roads.
	BuildSignals, ///< Price for building rail signals.
	BuildBridge, ///< Price for building bridges.
	BuildDepotTrain, ///< Price for building train depots.
	BuildDepotRoad, ///< Price for building road vehicle depots.
	BuildDepotShip, ///< Price for building ship depots.
	BuildTunnel, ///< Price for building tunnels.
	BuildStationRail, ///< Price for building rail stations.
	BuildStationRailLength, ///< Additional price for building rail stations dependent on their length.
	BuildStationAirport, ///< Price for building airports.
	BuildStationBus, ///< Price for building bus stops.
	BuildStationTruck, ///< Price for building lorry stations.
	BuildStationDock, ///< Price for building docks.
	BuildVehicleTrain, ///< Price for purchasing new train engines.
	BuildVehicleWagon, ///< Price for purchasing new wagons.
	BuildVehicleAircraft, ///< Price for purchasing new aircrafts.
	BuildVehicleRoad, ///< Price for purchasing new road vehicles.
	BuildVehicleShip, ///< Price for purchasing new ships.
	BuildTrees, ///< Price for planting trees.
	Terraform, ///< Price for terraforming land, e.g. rising, lowering and flattening.
	ClearGrass, ///< Price for destroying grass.
	ClearRough, ///< Price for destroying rough land.
	ClearRocks, ///< Price for destroying rocks.
	ClearFields, ///< Price for destroying fields.
	ClearTrees, ///< Price for destroying trees.
	ClearRail, ///< Price for destroying rails.
	ClearSignals, ///< Price for destroying rail signals.
	ClearBridge, ///< Price for destroying bridges.
	ClearDepotTrain, ///< Price for destroying train depots.
	ClearDepotRoad, ///< Price for destroying road vehicle depots.
	ClearDepotShip, ///< Price for destroying ship depots.
	ClearTunnel, ///< Price for destroying tunnels.
	ClearWater, ///< Price for destroying water e.g. see, rives.
	ClearStationRail, ///< Price for destroying rail stations.
	ClearStationAirport, ///< Price for destroying airports.
	ClearStationBus, ///< Price for destroying bus stops.
	ClearStationTruck, ///< Price for destroying lorry stations.
	ClearStationDock, ///< Price for destroying docks.
	ClearHouse, ///< Price for destroying houses and other town buildings.
	ClearRoad, ///< Price for destroying roads.
	RunningTrainSteam, ///< Running cost of steam trains.
	RunningTrainDiesel, ///< Running cost of diesel trains.
	RunningTrainElectric, ///< Running cost of electric trains.
	RunningAircraft, ///< Running cost of aircrafts.
	RunningRoadveh, ///< Running cost of road vehicles.
	RunningShip, ///< Running cost of ships.
	BuildIndustry, ///< Price for funding new industries.
	ClearIndustry, ///< Price for destroying industries.
	BuildObject, ///< Price for building new objects.
	ClearObject, ///< Price for destroying objects.
	BuildWaypointRail, ///< Price for building new rail waypoints.
	ClearWaypointRail, ///< Price for destroying rail waypoints.
	BuildWaypointBuoy, ///< Price for building new buoys.
	ClearWaypointBuoy, ///< Price for destroying buoys.
	TownAction, ///< Price for interaction with local authorities.
	BuildFoundation, ///< Price for building foundation under other constructions e.g. roads, rails, depots, objects, etc., etc..
	BuildIndustryRaw, ///< Price for funding new raw industries, e.g. coal mine, forest.
	BuildTown, ///< Price for funding new towns and cities.
	BuildCanal, ///< Price for building new canals.
	ClearCanal, ///< Price for destroying canals.
	BuildAqueduct, ///< Price for building new aqueducts.
	ClearAqueduct, ///< Price for destroying aqueducts.
	BuildLock, ///< Price for building new locks.
	ClearLock, ///< Price for destroying locks.
	InfrastructureRail, ///< Rails maintenance cost.
	InfrastructureRoad, ///< Roads maintenance cost.
	InfrastructureWater, ///< Canals maintenance cost.
	InfrastructureStation, ///< Stations maintenance cost.
	InfrastructureAirport, ///< Airports maintenance cost.
	End, ///< Price base end marker.
	Invalid = 0xFF ///< Invalid base price.
};

using Prices = EnumIndexArray<Money, Price, Price::End>; ///< Prices of everything. @see Price
using PriceMultipliers = EnumIndexArray<int8_t, Price, Price::End>;

/** Types of expenses. */
enum class ExpensesType : uint8_t {
	Construction =  0, ///< Construction costs.
	NewVehicles, ///< New vehicles.
	TrainRun, ///< Running costs trains.
	RoadVehRun, ///< Running costs road vehicles.
	AircraftRun, ///< Running costs aircraft.
	ShipRun, ///< Running costs ships.
	Property, ///< Property costs.
	TrainRevenue, ///< Revenue from trains.
	RoadVehRevenue, ///< Revenue from road vehicles.
	AircraftRevenue, ///< Revenue from aircraft.
	ShipRevenue, ///< Revenue from ships.
	LoanInterest, ///< Interest payments over the loan.
	Other, ///< Other expenses.
	End, ///< End marker.
	Begin = ExpensesType::Construction, ///< Begin marker.
	Invalid = 0xFF, ///< Invalid expense type.
};

/**
 * Data type for storage of Money for each #ExpensesType category.
 */
using Expenses = EnumIndexArray<Money, ExpensesType, ExpensesType::End>;

/**
 * Categories of a price bases.
 */
enum class PriceCategory : uint8_t {
	None, ///< Not affected by difficulty settings
	Running, ///< Price is affected by "vehicle running cost" difficulty setting
	Construction, ///< Price is affected by "construction cost" difficulty setting
};

/** The "steps" in loan size, in British Pounds! */
static const int LOAN_INTERVAL = 10000;
/** The size of loan for a new company, in British Pounds! */
static const int64_t INITIAL_LOAN = 100000;
/** The max amount possible to configure for a max loan of a company. */
static const int64_t MAX_LOAN_LIMIT = 2000000000;

/**
 * Maximum inflation (including fractional part) without causing overflows in int64_t price computations.
 * This allows for 32 bit base prices (21 are currently needed).
 * Considering the sign bit and 16 fractional bits, there are 15 bits left.
 * 170 years of 4% inflation result in a inflation of about 822, so 10 bits are actually enough.
 * Note that NewGRF multipliers share the 16 fractional bits.
 * @see MAX_PRICE_MODIFIER
 */
static const uint64_t MAX_INFLATION = (1ull << (63 - 32)) - 1;

/**
 * Maximum NewGRF price modifiers.
 * Increasing base prices by factor 65536 should be enough.
 * @see MAX_INFLATION
 */
static const int MIN_PRICE_MODIFIER = -8;
static const int MAX_PRICE_MODIFIER = 16;
static const int INVALID_PRICE_MODIFIER = MIN_PRICE_MODIFIER - 1;

/** Multiplier for how many regular track bits a tunnel/bridge counts. */
static const uint TUNNELBRIDGE_TRACKBIT_FACTOR = 4;
/** Multiplier for how many regular track bits a level crossing counts. */
static const uint LEVELCROSSING_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular track bits a road depot counts. */
static const uint ROAD_DEPOT_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular track bits a bay stop counts. */
static const uint ROAD_STOP_TRACKBIT_FACTOR = 2;
/** Multiplier for how many regular tiles a lock counts. */
static const uint LOCK_DEPOT_TILE_FACTOR = 2;

struct CargoPayment;
using CargoPaymentID = PoolID<uint32_t, struct CargoPaymentIDTag, 0xFF000, 0xFFFFF>;

#endif /* ECONOMY_TYPE_H */
