/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file accounts.h National accounts measured from game events. GDP is value added, not a sum of cargo pieces. */

#ifndef ECONOMYLAB_ACCOUNTS_H
#define ECONOMYLAB_ACCOUNTS_H

#include "units.hpp"

#include "../cargo_type.h"

#include <array>
#include <cstdint>

/** Open-period accumulators. Saved, so a reload does not forget the month in progress. */
struct EconomyLabPersistent {
	uint16_t schema = 1;
	uint8_t generation = 0; ///< 0 is the cargo-unit model. The first close migrates capital off that scale.
	uint8_t anchored = 0; ///< The policy scale is fixed. Potential output has its own level.
	int64_t sequence = 0; ///< Last accepted event id. A repeated id is rejected.
	int64_t ledger_watermark = 0; ///< Highest posted event id. Zero means no event has been accepted.
	int64_t ledger_debits = 0;
	int64_t ledger_credits = 0;
	uint8_t opening_booked = 0; ///< The opening balance sheet was closed once. Later months post flows.
	int64_t household_nominal = 0; ///< Passenger and mail tariffs, current pounds.
	int64_t freight_nominal = 0; ///< Other cargo tariffs, current pounds. Intermediate, not consumption.
	int64_t subsidy_nominal = 0; ///< Route subsidy paid on top of the tariff. A transfer, not government consumption.
	int64_t potential_base = 0; ///< Real pounds of GDP at calibration. 1000 policy points equal this amount.
	int64_t output_level = 0; ///< Potential output in real pounds. Zero means to use the policy scale.
	int64_t population_base = 0;
	int64_t capacity_base = 0;
	int64_t capital_base = 0; ///< Zero until the stock is positive. A base of 1 would explode E-027.
	int64_t price_base = 0; ///< Macro price index at calibration. PPP uses it.
	int64_t exchange_pm = 10000; ///< Pesos per US dollar. 10000 is 100 at the start. A higher number is a depreciation.
	int64_t fundamental_pm = 10000;
	int64_t exports = 0;
	int64_t imports = 0;
	int64_t world_gdp_pm = 10000; ///< Rest-of-world GDP index. 10000 is 100. It grows at the world growth setting.
	int64_t terms_of_trade_pm = 10000; ///< Export prices over import prices. 10000 is parity. A flat trend plus a bounded wave.
	int64_t money_anchor = 0; ///< Nominal money points when the real-balance gap was anchored. Zero until the first month.
	int64_t external_debt = 0; ///< Net liabilities to the rest of the world. Negative is a claim. Equal to minus the net foreign assets.
	int64_t world_growth_remainder = 0; ///< Unused fraction of rest-of-world growth, so 2.50 percent is not truncated away.
	int64_t wages = 0;
	int64_t dividends = 0;
	int64_t worker_consumption = 0;
	int64_t owner_consumption = 0;
	int64_t household_saving = 0;
	int64_t portfolio_money = 0;
	int64_t portfolio_private = 0;
	int64_t portfolio_public = 0;
	int64_t portfolio_external = 0;
	int64_t funding_gap = 0; ///< Public borrowing households did not buy. It raises the risk premium.
	int32_t parity_pressure_bp = 0; ///< Desired external bonds minus the current account, carried into next month's dollar.
	int32_t expected_devaluation_bp = 0; ///< Annual expected rise in the dollar, sized from the current-account gap.
	int32_t world_rate_bp = 200;
	int32_t risk_bp = 100;
	int32_t loan_rate_bp = 0;
	int64_t bank_loans = 0;
	int64_t bank_deposits = 0;
	int64_t bank_reserves = 0;
	int64_t bank_bonds = 0;
	int64_t bank_equity = 0;
	int64_t treasury_debt = 0;
	int64_t cb_bonds = 0;
	int64_t cb_reserve_liability = 0;
	int64_t official_reserves = 0;
	int64_t base_money = 0;
	int64_t household_bonds = 0;
	int64_t nfa = 0;
	/** Laboratory session carried in the ELAB chunk. A missing column reloads as a fresh session. */
	int32_t lab_tick = 0;
	int64_t session_y = 10000;
	int64_t session_tax = 2000;
	int64_t session_debt = 0;
	int64_t session_money = 10000;
	int64_t session_price = 10000;
	int64_t session_exchange = 9800;
	int32_t session_rate_bp = 400;
	int32_t session_pd_bp = 0;
	int32_t session_lgd = 5000;
	int64_t session_loans = 4000;
	int64_t session_reserves = 500;
	int64_t session_bonds = 500;
	int64_t session_deposits = 4800;
	int64_t session_equity = 200;
	int64_t session_provision = 0;
	int32_t session_clamps = 0;
	uint8_t session_ok = 1;
	int64_t government_extra = 0;
	int64_t import_extra = 0;
	uint64_t stream_shock = 0x9E3779B97F4A7C15ULL;
	uint64_t stream_trade = 0xBF58476D1CE4E5B9ULL;
	uint64_t stream_bank = 0x94D049BB133111EBULL;
	uint64_t session_watermark = 0;
	int64_t session_debits = 0;
	int64_t session_credits = 0;
	uint8_t pending_count = 0;
	uint8_t applied_count = 0;
	std::array<uint64_t, 8> pending_id{};
	std::array<int32_t, 8> pending_tick{};
	std::array<int64_t, 8> pending_magnitude{};
	std::array<uint8_t, 8> pending_variable{};
	std::array<uint8_t, 8> pending_operation{};
	std::array<uint64_t, 8> applied_id{};
	std::array<int32_t, 8> applied_tick{};
	std::array<int64_t, 8> applied_magnitude{};
	std::array<uint8_t, 8> applied_variable{};
	std::array<uint8_t, 8> applied_operation{};
	uint8_t policy_shock_count = 0;
	std::array<uint64_t, 16> policy_shock_id{};
	std::array<int32_t, 16> policy_shock_tick{};
	std::array<int64_t, 16> policy_shock_magnitude{};
	std::array<uint8_t, 16> policy_shock_variable{};
	int64_t employment = 0;
	int64_t labour_force = 0;
	std::array<int64_t, 64> delivered{}; ///< Physical units by cargo type. Never added into GDP.
};

/** Flows known at the monthly close, already separated by economic meaning. */
struct EconomyLabFlows {
	int64_t capex_pounds = 0; ///< Construction and new vehicles. Gross fixed capital formation.
	int64_t opex_pounds = 0; ///< Infrastructure maintenance. Intermediate consumption, not investment.
	int64_t government_points = 0; ///< Purchase setting only. Subsidies are not added here.
	int64_t tax_rate_percent = 0;
	int64_t autonomous_tax_points = 0;
	int64_t population = 0;
	int64_t capacity = 0; ///< Installed vehicle cargo slots. A logistics indicator, not GDP.
	int64_t capital_stock = 0; ///< Real-pound capital stock at the start of the close.
	int64_t payment_index = PAYMENT_INDEX_BASE;
	int32_t unemployment_bp = 500; ///< Okun rate from last month. 500 is the natural rate, so labour starts at the headcount.
};

/** Published accounts for one close. Discrepancy is explicit and is not folded into consumption. */
struct EconomyLabAccounts {
	bool ok = false;
	int64_t gdp_production = 0; ///< E-006, real pounds.
	int64_t gdp_expenditure = 0; ///< E-007 with X = M = 0 until the external sector exists.
	int64_t discrepancy = 0; ///< E-008. Production minus expenditure.
	int64_t consumption = 0; ///< Household transport services, real pounds.
	int64_t investment = 0; ///< Capex only, real pounds.
	int64_t government = 0; ///< Policy purchases, real pounds.
	int64_t subsidies = 0; ///< Product subsidies, real pounds. Subtracted in E-006.
	int64_t intermediate = 0; ///< Freight transport bought by producers, real pounds.
	int64_t value_added = 0; ///< Transport GO minus maintenance.
	int64_t tax_revenue = 0;
	int64_t potential = 0; ///< Integer form of E-027.
	int64_t logistics_pieces = 0; ///< Sum of physical units. Not GDP.
	bool reset_legacy_capital = false;
	bool recalibrate = false; ///< Schema 1 used a pound scale and a capital base that do not match GDP.
	bool level_rebased = false; ///< Potential output was recentered on this month's GDP. Policy points were not rescaled.
	bool ledger_balanced = false;
};

/** Open-economy and bank balances published with the monthly accounts. Equity closes the bank identity. */
struct EconomyLabOpen {
	int64_t exports = 0;
	int64_t imports = 0;
	int64_t exchange_pm = 10000;
	int64_t fundamental_pm = 10000;
	int32_t world_rate_bp = 200;
	int32_t risk_bp = 100;
	int32_t loan_rate_bp = 0;
	int64_t bank_loans = 0;
	int64_t bank_deposits = 0;
	int64_t bank_reserves = 0;
	int64_t bank_bonds = 0;
	int64_t bank_equity = 0;
	int64_t bank_gap = 0;
	int64_t employment = 0;
	int64_t labour_force = 0;
	int64_t discrepancy = 0;
	int64_t treasury_gap = 0;
	int64_t cb_gap = 0;
	int64_t external_gap = 0;
	int64_t base_money = 0;
	int64_t official_reserves = 0;
	int64_t household_bonds = 0;
	int64_t nfa = 0;
	bool identity_ok = false;
	bool overshooting = false;
	int64_t world_gdp_pm = 10000;
	int64_t terms_of_trade_pm = 10000;
	int64_t external_debt = 0;
	int64_t external_service = 0;
	int32_t expected_devaluation_bp = 0;
	int64_t wages = 0;
	int64_t dividends = 0;
	int64_t worker_consumption = 0;
	int64_t owner_consumption = 0;
	int64_t household_saving = 0;
	int64_t portfolio_money = 0;
	int64_t portfolio_private = 0;
	int64_t portfolio_public = 0;
	int64_t portfolio_external = 0;
	int64_t funding_gap = 0;
};

/** One evaluation of goods demand. It does not write the persistent stocks. */
struct EconomyLabDemand {
	int64_t demand = 0; ///< C + I + G + X − M plus the real-balance gap and the interest-rate effect.
	int64_t real_money = 0; ///< Nominal money points deflated by the price index, still in points.
	int64_t money_demand = 0; ///< Income-scaled anchor, in points. The LM curve turns this into a rate.
	int64_t money_impulse = 0; ///< Real-balance gap passed into goods demand, in real pounds.
	int64_t interest_effect = 0; ///< Demand removed by a policy rate above 4 percent.
	int64_t exports = 0;
	int64_t imports = 0;
	int64_t exchange_pm = 10000;
	int64_t terms_of_trade_pm = 10000;
	int64_t world_gdp_pm = 10000;
	int64_t external_service = 0;
	int32_t expected_devaluation_bp = 0;
	int32_t world_rate_bp = 200;
	int64_t wages = 0;
	int64_t dividends = 0;
	int64_t worker_consumption = 0;
	int64_t owner_consumption = 0;
	int64_t saving = 0;
	int64_t consumption = 0; ///< Workers, shareholders, autonomous consumption and the real-balance effect.
	int64_t investment = 0; ///< Measured capex, autonomous investment and the interest-rate effect.
};

/** Wages, profits, dividends and the two classes of consumption and saving. */
struct EconomyLabIncome {
	int64_t wages = 0;
	int64_t profits = 0;
	int64_t dividends = 0;
	int64_t worker_consumption = 0;
	int64_t owner_consumption = 0;
	int64_t worker_saving = 0;
	int64_t owner_saving = 0;
	int64_t consumption = 0;
	int64_t saving = 0;
};

/** One month of household saving, after money demand. The four flows add up to saving. */
struct EconomyLabPortfolio {
	int64_t money = 0;
	int64_t private_bonds = 0;
	int64_t public_bonds = 0;
	int64_t external_bonds = 0;
	int64_t funding_gap = 0;
};

EconomyLabPersistent &EconomyLabPersistentState();
void EconomyLabReset();

/** Same numeric codes as lab::ShockVariable. A policy click is an audited shock, not a second solver pass. */
static constexpr uint8_t ECONOMYLAB_SHOCK_MONEY = 0;
static constexpr uint8_t ECONOMYLAB_SHOCK_GOVERNMENT = 1;
static constexpr uint8_t ECONOMYLAB_SHOCK_WORLDRATE = 3;
static constexpr uint8_t ECONOMYLAB_SHOCK_SUBSIDIES = 6;
static constexpr uint8_t ECONOMYLAB_SHOCK_TAX = 7;
static constexpr uint8_t ECONOMYLAB_SHOCK_AUTONOMOUS_TAX = 8;

/** Record one policy click. The effective tick is the economy date. Repeated calls append a new id. */
bool EconomyLabRecordPolicyShock(uint8_t variable, int32_t effective_tick, int64_t magnitude);

/** Put the session columns back to a fresh game before an old save overwrites the ones it has. */
void EconomyLabRestoreFreshSession();

/** One balanced event. A repeated or non-positive id does not move the totals. */
bool EconomyLabPostEvent(uint64_t id, int64_t amount);

/** One accepted delivery. @p tariff_pounds excludes the subsidy. */
void EconomyLabNoteDelivery(CargoType cargo, int64_t units, int64_t tariff_pounds);

/** Extra profit paid by the subsidy. A transfer, recorded once. */
void EconomyLabNoteSubsidy(int64_t pounds);

/**
 * Close the open period into national accounts.
 * @param commit Clear the open flows and, on the first positive GDP, fix the potential base.
 */
EconomyLabAccounts EconomyLabCloseMonth(const EconomyLabFlows &flows, bool commit);

/** Post the month's production and expenditure, including trade, on the persistent ledger. */
bool EconomyLabSealIdentity(const EconomyLabAccounts &accounts, int64_t exports, int64_t imports);

/**
 * Recenter potential output on @p gdp when the production function can no longer express the gap.
 * The policy scale (1000 points = calibration GDP) is left unchanged.
 * @return true when the level moved.
 */
bool EconomyLabRebaseOutputLevel(int64_t gdp, int64_t capital, int64_t population, int64_t capacity);

/**
 * Grow rest-of-world GDP one month, step the dollar's fundamental toward PPP, and anchor nominal money once.
 * Call this once per economy month, before reading demand.
 */
void EconomyLabAdvanceWorld(int32_t date, int64_t price_index);

/**
 * Goods demand at one policy rate and one price index.
 * Exports follow rest-of-world GDP. Imports follow local GDP. The dollar moves with the current-account gap.
 * Nothing in the persistent stock is written.
 */
EconomyLabDemand EconomyLabPreviewDemand(const EconomyLabAccounts &accounts, int32_t policy_rate_bp, int64_t price_index, int64_t public_debt);

/** LM rate, in hundredths of a percent, that clears money demand at this output and the current price index. */
int32_t EconomyLabRateAtOutput(int64_t output, int64_t price_index);

/**
 * E-043 to E-053 and the bank balance, after the policy rate is known.
 * Exports and imports enter the expenditure identity. External bonds absorb the current-account gap at the world rate.
 * Bank equity is the residual that makes assets equal liabilities.
 */
EconomyLabOpen EconomyLabApplyOpenEconomy(const EconomyLabAccounts &accounts, int32_t policy_rate_bp, int64_t price_index, int64_t public_debt, int64_t loan_pounds, int64_t population, int32_t unemployment_bp);

const EconomyLabOpen &EconomyLabOpenState();

/** Split GDP into wages and profits, pay dividends, and apply the two marginal propensities. */
EconomyLabIncome EconomyLabDistributeIncome(int64_t gdp, int64_t interest_paid, int64_t tax, int wage_share_percent, int payout_percent, int worker_mpc_percent, int owner_mpc_percent);

/**
 * Place saving into money, then into bonds.
 * Money takes the gap between money demand and balances. The rest is split by uncovered interest parity.
 * Domestic bonds fund the public deficit first. What remains funds private loans. The foreign share rises when the world return exceeds the domestic rate.
 */
EconomyLabPortfolio EconomyLabAllocateSaving(int64_t saving, int64_t money_gap, int64_t public_need, int32_t domestic_bp, int32_t foreign_bp);

/** Grow an index by an annual rate in hundredths of a percent. The remainder keeps the fractional part for the next month. */
int64_t EconomyLabGrowIndex(int64_t level, int32_t annual_bp, int64_t &remainder);

/** Terms of trade around 10000. The wave is ±5 percent and lasts 360 economy days. */
int64_t EconomyLabTermsOfTrade(int32_t date);

/** Headcount adjusted by Okun unemployment. At 5 percent the result is the headcount. */
int64_t EconomyLabEffectiveLabour(int64_t population, int32_t unemployment_bp);

#endif /* ECONOMYLAB_ACCOUNTS_H */
