/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy.h Monthly national accounts, Taylor rule and Phillips curve. */

#ifndef MACRO_ECONOMY_H
#define MACRO_ECONOMY_H

#include "cargo_type.h"

static constexpr int MACRO_HISTORY_MONTHS = 24;
static constexpr int MACRO_HIST_SERIES = 8;
static constexpr int MACRO_HIST_INFLATION = 0;
static constexpr int MACRO_HIST_PUBLIC_DEBT = 1;
static constexpr int MACRO_HIST_PRIVATE_DEBT = 2;
static constexpr int MACRO_HIST_OUTPUT = 3;
static constexpr int MACRO_HIST_POTENTIAL = 4;
static constexpr int MACRO_HIST_INVESTMENT = 5;
static constexpr int MACRO_HIST_GOVERNMENT = 6;
static constexpr int MACRO_HIST_SUBSIDIES = 7;
static constexpr int MACRO_HIST2_PRICE = 8;
static constexpr int MACRO_HIST2_REAL_MONEY = 9;
static constexpr int MACRO_HIST2_MONEY_DEMAND = 10;
static constexpr int MACRO_HIST2_CONSUMPTION = 11;
static constexpr int MACRO_HIST2_INTEREST = 12;
static constexpr int MACRO_HIST2_UNEMPLOYMENT = 13;
static constexpr int MACRO_HIST2_TAX = 14;
static constexpr int MACRO_HIST2_EXOGENOUS = 15;

void ResetMacroEconomy();
void RefreshMacroEconomy();
void MacroEconomyMonthly();
void MacroEconomyObserve();

/** Extra profit paid because a delivery was subsidised, in pounds. */
void MacroNoteSubsidyPayment(int64_t pounds);

/** One accepted delivery. The tariff is the payment before any route subsidy. Pieces are not GDP. */
void MacroNoteCargoDelivery(CargoType cargo, int64_t units, int64_t tariff_pounds);

/** Short name of one macro graph series. Series 0 to 15. */
const char *MacroSeriesName(int series);

/** Months stored for the macro graphs. */
int MacroHistoryCount();

/**
 * One point of a macro graph.
 * @param series One of the MACRO_HIST_* series.
 * @param age 0 is the newest stored month.
 */
int32_t MacroHistoryValue(int series, int age);

/**
 * Production multiplier implied by the output gap, in permyriad.
 * 10000 leaves town and industry production unchanged.
 */
uint MacroActivityPermyriad();

/** Extra town cargo where population is growing. 10000 leaves production unchanged. */
uint MacroTownIncentivePermyriad(bool growing, uint16_t growth_rate);

/** Extra industry cargo where production is above the level the industry was born with. */
uint MacroIndustryIncentivePermyriad(int production_level);

/** Output on the IS curve at an interest rate, in hundredths of a percent. */
int32_t MacroOutputAtInterest(int32_t interest_bp);

/** Interest on the LM curve at an output level, in hundredths of a percent. */
int32_t MacroInterestAtOutput(int32_t output);

/** Output on the aggregate-demand curve at a stored price index. */
int32_t MacroOutputAtPrice(int32_t price_index);

/** Price index on the short-run aggregate-supply curve at an output level. */
int32_t MacroPriceAtOutput(int32_t output);

#endif /* MACRO_ECONOMY_H */
