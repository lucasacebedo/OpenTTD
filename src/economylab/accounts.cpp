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
	if (ratio < 1000) return 1000;
	if (ratio > 100000) return 100000;
	return ratio;
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
	const int64_t government_real = PolicyToReal(PolicyPoints{std::max<int64_t>(flows.government_points, 0)}).Raw();

	/* Transport sales: buyers are debited, the carrier is credited. Maintenance and capex leave the carrier's cash. */
	const int64_t transport_go = household_real + freight_real;
	const int64_t ledger_debits = household_real + freight_real + opex_real + capex_real + subsidy_real + government_real;
	const int64_t ledger_credits = transport_go + opex_real + capex_real + subsidy_real + government_real;
	accounts.ledger_balanced = ledger_debits == ledger_credits;

	const int64_t value_added = transport_go - opex_real;
	/* E-006. Product taxes are not observed yet. Product subsidies are the route payments. */
	const int64_t gdp_production = value_added - subsidy_real;
	/* E-007. Exports and imports stay zero until the external sector exists. */
	const int64_t gdp_expenditure = household_real + capex_real + government_real;
	accounts.gdp_production = gdp_production;
	accounts.gdp_expenditure = gdp_expenditure;
	accounts.discrepancy = gdp_production - gdp_expenditure;
	accounts.consumption = household_real;
	accounts.investment = capex_real;
	accounts.government = government_real;
	accounts.subsidies = subsidy_real;
	accounts.intermediate = freight_real;
	accounts.value_added = value_added;
	accounts.ok = accounts.ledger_balanced;

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

	if (commit && _lab.anchored == 0 && gdp_production > 0) {
		_lab.potential_base = gdp_production;
		_lab.population_base = std::max<int64_t>(flows.population, 1);
		_lab.capacity_base = std::max<int64_t>(flows.capacity, 1);
		_lab.capital_base = std::max<int64_t>(capital, 1);
		_lab.anchored = 1;
	}
	accounts.potential = PotentialReal(capital, flows.population, flows.capacity);
	if (accounts.potential <= 0 && gdp_production > 0 && _lab.anchored != 0) accounts.potential = _lab.potential_base;

	const int64_t taxable = std::max<int64_t>(gdp_production, 0);
	accounts.tax_revenue = PolicyToReal(PolicyPoints{std::max<int64_t>(flows.autonomous_tax_points, 0)}).Raw() + flows.tax_rate_percent * taxable / 100;

	if (commit) {
		_lab.household_nominal = 0;
		_lab.freight_nominal = 0;
		_lab.subsidy_nominal = 0;
		_lab.delivered.fill(0);
	}
	return accounts;
}
