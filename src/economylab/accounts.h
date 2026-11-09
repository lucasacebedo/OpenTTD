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
	uint8_t anchored = 0; ///< Potential base is fixed. It is never copied from observed GDP again.
	int64_t sequence = 0; ///< Deliveries and subsidy notes seen. Exactly-once is the caller's duty.
	int64_t household_nominal = 0; ///< Passenger and mail tariffs, current pounds.
	int64_t freight_nominal = 0; ///< Other cargo tariffs, current pounds. Intermediate, not consumption.
	int64_t subsidy_nominal = 0; ///< Route subsidy paid on top of the tariff. A transfer, not government consumption.
	int64_t potential_base = 0; ///< Real pounds of GDP at the calibration month.
	int64_t population_base = 0;
	int64_t capacity_base = 0;
	int64_t capital_base = 0;
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
	bool ledger_balanced = false;
};

EconomyLabPersistent &EconomyLabPersistentState();
void EconomyLabReset();

/** One accepted delivery. @p tariff_pounds excludes the subsidy. */
void EconomyLabNoteDelivery(CargoType cargo, int64_t units, int64_t tariff_pounds);

/** Extra profit paid by the subsidy. A transfer, recorded once. */
void EconomyLabNoteSubsidy(int64_t pounds);

/**
 * Close the open period into national accounts.
 * @param commit Clear the open flows and, on the first positive GDP, fix the potential base.
 */
EconomyLabAccounts EconomyLabCloseMonth(const EconomyLabFlows &flows, bool commit);

#endif /* ECONOMYLAB_ACCOUNTS_H */
