/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file economy_sl.cpp Code handling saving and loading of economy data. */

#include "../stdafx.h"

#include "saveload.h"
#include "saveload_error.hpp"
#include "compat/economy_sl_compat.h"

#include "../economy_func.h"
#include "../economy_base.h"
#include "../vehicle_base.h"

#include "../safeguards.h"

/** Prices in pre 126 savegames */
struct PRICChunkHandler : ChunkHandler {
	PRICChunkHandler() : ChunkHandler("PRIC", ChunkType::ReadOnly) {}

	void Load() const override
	{
		size_t num_prices = 49;
		size_t record_size = (IsSavegameVersionBefore(SaveLoadVersion::UnifyCurrency) ? sizeof(uint32_t) : sizeof(uint64_t)) + sizeof(uint16_t);
		SlSkipBytes(num_prices * record_size);
	}
};

/** Cargo payment rates in pre 126 savegames */
struct CAPRChunkHandler : ChunkHandler {
	CAPRChunkHandler() : ChunkHandler("CAPR", ChunkType::ReadOnly) {}

	void Load() const override
	{
		size_t num_cargo = IsSavegameVersionBefore(SaveLoadVersion::NewGRFCargo) ? 12 : IsSavegameVersionBefore(SaveLoadVersion::ExtendCargotypes) ? 32 : NUM_CARGO;
		size_t record_size = sizeof(uint16_t) + (IsSavegameVersionBefore(SaveLoadVersion::UnifyCurrency) ? sizeof(uint32_t) : sizeof(uint64_t));
		SlSkipBytes(num_cargo * record_size);
	}
};

static const SaveLoad _economy_desc[] = {
	SaveLoad::Variable<VarFileType::I32>("old_max_loan_unround", SLE_OBJECT_ADDRESS(Economy, old_max_loan_unround), SaveLoadVersion::MinVersion, SaveLoadVersion::UnifyCurrency),
	SaveLoad::Variable<VarFileType::I64>("old_max_loan_unround", SLE_OBJECT_ADDRESS(Economy, old_max_loan_unround), SaveLoadVersion::UnifyCurrency, SaveLoadVersion::CumulatedInflation),
	SaveLoad::Variable<VarFileType::U16>("old_max_loan_unround_fract", SLE_OBJECT_ADDRESS(Economy, old_max_loan_unround_fract), SaveLoadVersion::CargoPaymentOverflow, SaveLoadVersion::CumulatedInflation),
	SaveLoad::Variable<VarFileType::U64>("inflation_prices", SLE_OBJECT_ADDRESS(Economy, inflation_prices), SaveLoadVersion::CumulatedInflation),
	SaveLoad::Variable<VarFileType::U64>("inflation_payment", SLE_OBJECT_ADDRESS(Economy, inflation_payment), SaveLoadVersion::CumulatedInflation),
	SaveLoad::Variable<VarFileType::I16>("fluct", SLE_OBJECT_ADDRESS(Economy, fluct)),
	SaveLoad::Variable<VarFileType::U8>("interest_rate", SLE_OBJECT_ADDRESS(Economy, interest_rate)),
	SaveLoad::Variable<VarFileType::U8>("infl_amount", SLE_OBJECT_ADDRESS(Economy, infl_amount)),
	SaveLoad::Variable<VarFileType::U8>("infl_amount_pr", SLE_OBJECT_ADDRESS(Economy, infl_amount_pr)),
	SaveLoad::Variable<VarFileType::U32>("industry_daily_change_counter", SLE_OBJECT_ADDRESS(Economy, industry_daily_change_counter), SaveLoadVersion::SpreadIndustryProductionChanges),
	SaveLoad::Variable<VarFileType::I32>("macro_output", SLE_OBJECT_ADDRESS(Economy, macro_output), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_interest_bp", SLE_OBJECT_ADDRESS(Economy, macro_interest_bp), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_inflation_bp", SLE_OBJECT_ADDRESS(Economy, macro_inflation_bp), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_expected_inflation_bp", SLE_OBJECT_ADDRESS(Economy, macro_expected_inflation_bp), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_price_index", SLE_OBJECT_ADDRESS(Economy, macro_price_index), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_output_gap_bp", SLE_OBJECT_ADDRESS(Economy, macro_output_gap_bp), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_consumption", SLE_OBJECT_ADDRESS(Economy, macro_consumption), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_investment", SLE_OBJECT_ADDRESS(Economy, macro_investment), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_tax_revenue", SLE_OBJECT_ADDRESS(Economy, macro_tax_revenue), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_real_money", SLE_OBJECT_ADDRESS(Economy, macro_real_money), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::U8>("macro_ok", SLE_OBJECT_ADDRESS(Economy, macro_ok), SaveLoadVersion::MacroIsLm),
	SaveLoad::Variable<VarFileType::I32>("macro_natural_output", SLE_OBJECT_ADDRESS(Economy, macro_natural_output), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I32>("macro_unemployment_bp", SLE_OBJECT_ADDRESS(Economy, macro_unemployment_bp), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I32>("macro_population_growth_bp", SLE_OBJECT_ADDRESS(Economy, macro_population_growth_bp), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I32>("macro_company_investment", SLE_OBJECT_ADDRESS(Economy, macro_company_investment), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I32>("macro_debt_service", SLE_OBJECT_ADDRESS(Economy, macro_debt_service), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I32>("macro_lagged_inflation_bp", SLE_OBJECT_ADDRESS(Economy, macro_lagged_inflation_bp), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_debt", SLE_OBJECT_ADDRESS(Economy, macro_debt), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_capital", SLE_OBJECT_ADDRESS(Economy, macro_capital), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_population", SLE_OBJECT_ADDRESS(Economy, macro_population), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_population_snapshot", SLE_OBJECT_ADDRESS(Economy, macro_population_snapshot), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_population_base", SLE_OBJECT_ADDRESS(Economy, macro_population_base), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_cargo_capacity", SLE_OBJECT_ADDRESS(Economy, macro_cargo_capacity), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_cargo_capacity_base", SLE_OBJECT_ADDRESS(Economy, macro_cargo_capacity_base), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I64>("macro_company_spent_year", SLE_OBJECT_ADDRESS(Economy, macro_company_spent_year), SaveLoadVersion::MacroDynamics),
	SaveLoad::Variable<VarFileType::I32>("macro_cargo_potential", SLE_OBJECT_ADDRESS(Economy, macro_cargo_potential), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I32>("macro_government", SLE_OBJECT_ADDRESS(Economy, macro_government), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I32>("macro_game_output", SLE_OBJECT_ADDRESS(Economy, macro_game_output), SaveLoadVersion::MacroAccounts),
	SaveLoad::Variable<VarFileType::I32>("macro_exogenous_output", SLE_OBJECT_ADDRESS(Economy, macro_exogenous_output), SaveLoadVersion::MacroAccounts),
	SaveLoad::Variable<VarFileType::I32>("macro_money_demand", SLE_OBJECT_ADDRESS(Economy, macro_money_demand), SaveLoadVersion::MacroAccounts),
	SaveLoad::Variable<VarFileType::I32>("macro_demand_anchor", SLE_OBJECT_ADDRESS(Economy, macro_demand_anchor), SaveLoadVersion::MacroAccounts),
	SaveLoad::Variable<VarFileType::I32>("macro_game_anchor", SLE_OBJECT_ADDRESS(Economy, macro_game_anchor), SaveLoadVersion::MacroAccounts),
	SaveLoad::Variable<VarFileType::U8>("macro_anchor_set", SLE_OBJECT_ADDRESS(Economy, macro_anchor_set), SaveLoadVersion::MacroAccounts),
	SaveLoad::Variable<VarFileType::I32>("macro_subsidy_game", SLE_OBJECT_ADDRESS(Economy, macro_subsidy_game), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I32>("macro_bond_issue", SLE_OBJECT_ADDRESS(Economy, macro_bond_issue), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I32>("macro_bond_buyback", SLE_OBJECT_ADDRESS(Economy, macro_bond_buyback), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I32>("macro_private_debt_delta", SLE_OBJECT_ADDRESS(Economy, macro_private_debt_delta), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I32>("macro_private_interest", SLE_OBJECT_ADDRESS(Economy, macro_private_interest), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I64>("macro_private_debt", SLE_OBJECT_ADDRESS(Economy, macro_private_debt), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::I64>("macro_subsidy_pounds", SLE_OBJECT_ADDRESS(Economy, macro_subsidy_pounds), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::U8>("macro_bond_count", SLE_OBJECT_ADDRESS(Economy, macro_bond_count), SaveLoadVersion::MacroLab),
	SaveLoad::Variable<VarFileType::U8>("macro_history_count", SLE_OBJECT_ADDRESS(Economy, macro_history_count), SaveLoadVersion::MacroLab),
	SaveLoad::Array<VarFileType::I32, 192>("macro_history", SLE_OBJECT_ADDRESS(Economy, macro_history), SaveLoadVersion::MacroLab),
	SaveLoad::Array<VarFileType::I32, 192>("macro_history_prices", SLE_OBJECT_ADDRESS(Economy, macro_history_prices), SaveLoadVersion::MacroAccounts),
	SaveLoad::Array<VarFileType::I32, 48>("macro_bond_amount", SLE_OBJECT_ADDRESS(Economy, macro_bond_amount), SaveLoadVersion::MacroLab),
	SaveLoad::Array<VarFileType::I16, 48>("macro_bond_coupon", SLE_OBJECT_ADDRESS(Economy, macro_bond_coupon), SaveLoadVersion::MacroLab),
};

/** Economy variables */
struct ECMYChunkHandler : ChunkHandler {
	ECMYChunkHandler() : ChunkHandler("ECMY", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_economy_desc);

		SlSetArrayIndex(0);
		SlObject(&_economy, _economy_desc);
	}


	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_economy_desc, _economy_sl_compat);

		if (!IsSavegameVersionBefore(SaveLoadVersion::RiffToArray) && SlIterateArray() == -1) return;
		SlObject(&_economy, slt);
		if (!IsSavegameVersionBefore(SaveLoadVersion::RiffToArray) && SlIterateArray() != -1) SlErrorCorrupt("Too many ECMY entries");

		StartupIndustryDailyChanges(IsSavegameVersionBefore(SaveLoadVersion::SpreadIndustryProductionChanges)); // old savegames will need to be initialized
	}
};

static const SaveLoad _cargopayment_desc[] = {
	SaveLoad::Reference<SLRefType::Vehicle>("front", SLE_OBJECT_ADDRESS(CargoPayment, front)),
	SaveLoad::Variable<VarFileType::I64>("route_profit", SLE_OBJECT_ADDRESS(CargoPayment, route_profit)),
	SaveLoad::Variable<VarFileType::I64>("visual_profit", SLE_OBJECT_ADDRESS(CargoPayment, visual_profit)),
	SaveLoad::Variable<VarFileType::I64>("visual_transfer", SLE_OBJECT_ADDRESS(CargoPayment, visual_transfer), SaveLoadVersion::CargoReservation),
};

struct CAPYChunkHandler : ChunkHandler {
	CAPYChunkHandler() : ChunkHandler("CAPY", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_cargopayment_desc);

		for (CargoPayment *cp : CargoPayment::Iterate()) {
			SlSetArrayIndex(cp->index);
			SlObject(cp, _cargopayment_desc);
		}
	}

	void Load() const override
	{
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_cargopayment_desc, _cargopayment_sl_compat);

		int index;

		while ((index = SlIterateArray()) != -1) {
			CargoPayment *cp = CargoPayment::CreateAtIndex(CargoPaymentID(index));
			SlObject(cp, slt);
		}
	}

	void FixPointers() const override
	{
		for (CargoPayment *cp : CargoPayment::Iterate()) {
			SlObject(cp, _cargopayment_desc);
		}
	}
};

static const CAPYChunkHandler CAPY;
static const PRICChunkHandler PRIC;
static const CAPRChunkHandler CAPR;
static const ECMYChunkHandler ECMY;
static const ChunkHandlerRef economy_chunk_handlers[] = {
	CAPY,
	PRIC,
	CAPR,
	ECMY,
};

extern const ChunkHandlerTable _economy_chunk_handlers(economy_chunk_handlers);
