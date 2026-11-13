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
#include "../economylab/accounts.h"

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
	SaveLoad::Variable<VarFileType::I32>("macro_loan_rate_bp", SLE_OBJECT_ADDRESS(Economy, macro_loan_rate_bp), SaveLoadVersion::MacroBehavior),
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
	SaveLoad::Variable<VarFileType::I32>("macro_goods_demand", SLE_OBJECT_ADDRESS(Economy, macro_goods_demand), SaveLoadVersion::MacroWorld),
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

static const SaveLoad _economylab_desc[] = {
	SaveLoad::Variable<VarFileType::U16>("schema", SLE_OBJECT_ADDRESS(EconomyLabPersistent, schema)),
	SaveLoad::Variable<VarFileType::U8>("generation", SLE_OBJECT_ADDRESS(EconomyLabPersistent, generation)),
	SaveLoad::Variable<VarFileType::U8>("anchored", SLE_OBJECT_ADDRESS(EconomyLabPersistent, anchored)),
	SaveLoad::Variable<VarFileType::I64>("sequence", SLE_OBJECT_ADDRESS(EconomyLabPersistent, sequence)),
	SaveLoad::Variable<VarFileType::I64>("household_nominal", SLE_OBJECT_ADDRESS(EconomyLabPersistent, household_nominal)),
	SaveLoad::Variable<VarFileType::I64>("freight_nominal", SLE_OBJECT_ADDRESS(EconomyLabPersistent, freight_nominal)),
	SaveLoad::Variable<VarFileType::I64>("subsidy_nominal", SLE_OBJECT_ADDRESS(EconomyLabPersistent, subsidy_nominal)),
	SaveLoad::Variable<VarFileType::I64>("potential_base", SLE_OBJECT_ADDRESS(EconomyLabPersistent, potential_base)),
	SaveLoad::Variable<VarFileType::I64>("population_base", SLE_OBJECT_ADDRESS(EconomyLabPersistent, population_base)),
	SaveLoad::Variable<VarFileType::I64>("capacity_base", SLE_OBJECT_ADDRESS(EconomyLabPersistent, capacity_base)),
	SaveLoad::Variable<VarFileType::I64>("capital_base", SLE_OBJECT_ADDRESS(EconomyLabPersistent, capital_base)),
	SaveLoad::Variable<VarFileType::I64>("price_base", SLE_OBJECT_ADDRESS(EconomyLabPersistent, price_base)),
	SaveLoad::Variable<VarFileType::I64>("exchange_pm", SLE_OBJECT_ADDRESS(EconomyLabPersistent, exchange_pm)),
	SaveLoad::Variable<VarFileType::I64>("fundamental_pm", SLE_OBJECT_ADDRESS(EconomyLabPersistent, fundamental_pm)),
	SaveLoad::Variable<VarFileType::I64>("exports", SLE_OBJECT_ADDRESS(EconomyLabPersistent, exports)),
	SaveLoad::Variable<VarFileType::I64>("imports", SLE_OBJECT_ADDRESS(EconomyLabPersistent, imports)),
	SaveLoad::Variable<VarFileType::I32>("world_rate_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, world_rate_bp)),
	SaveLoad::Variable<VarFileType::I32>("risk_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, risk_bp)),
	SaveLoad::Variable<VarFileType::I32>("loan_rate_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, loan_rate_bp)),
	SaveLoad::Variable<VarFileType::I64>("bank_loans", SLE_OBJECT_ADDRESS(EconomyLabPersistent, bank_loans)),
	SaveLoad::Variable<VarFileType::I64>("bank_deposits", SLE_OBJECT_ADDRESS(EconomyLabPersistent, bank_deposits)),
	SaveLoad::Variable<VarFileType::I64>("bank_reserves", SLE_OBJECT_ADDRESS(EconomyLabPersistent, bank_reserves)),
	SaveLoad::Variable<VarFileType::I64>("bank_bonds", SLE_OBJECT_ADDRESS(EconomyLabPersistent, bank_bonds)),
	SaveLoad::Variable<VarFileType::I64>("bank_equity", SLE_OBJECT_ADDRESS(EconomyLabPersistent, bank_equity)),
	SaveLoad::Variable<VarFileType::I64>("employment", SLE_OBJECT_ADDRESS(EconomyLabPersistent, employment)),
	SaveLoad::Variable<VarFileType::I64>("labour_force", SLE_OBJECT_ADDRESS(EconomyLabPersistent, labour_force)),
	SaveLoad::Variable<VarFileType::I64>("output_level", SLE_OBJECT_ADDRESS(EconomyLabPersistent, output_level)),
	SaveLoad::Variable<VarFileType::I64>("ledger_watermark", SLE_OBJECT_ADDRESS(EconomyLabPersistent, ledger_watermark)),
	SaveLoad::Variable<VarFileType::I64>("ledger_debits", SLE_OBJECT_ADDRESS(EconomyLabPersistent, ledger_debits)),
	SaveLoad::Variable<VarFileType::I64>("ledger_credits", SLE_OBJECT_ADDRESS(EconomyLabPersistent, ledger_credits)),
	SaveLoad::Variable<VarFileType::U8>("opening_booked", SLE_OBJECT_ADDRESS(EconomyLabPersistent, opening_booked)),
	SaveLoad::Variable<VarFileType::I64>("treasury_debt", SLE_OBJECT_ADDRESS(EconomyLabPersistent, treasury_debt)),
	SaveLoad::Variable<VarFileType::I64>("cb_bonds", SLE_OBJECT_ADDRESS(EconomyLabPersistent, cb_bonds)),
	SaveLoad::Variable<VarFileType::I64>("cb_reserve_liability", SLE_OBJECT_ADDRESS(EconomyLabPersistent, cb_reserve_liability)),
	SaveLoad::Variable<VarFileType::I64>("official_reserves", SLE_OBJECT_ADDRESS(EconomyLabPersistent, official_reserves)),
	SaveLoad::Variable<VarFileType::I64>("base_money", SLE_OBJECT_ADDRESS(EconomyLabPersistent, base_money)),
	SaveLoad::Variable<VarFileType::I64>("household_bonds", SLE_OBJECT_ADDRESS(EconomyLabPersistent, household_bonds)),
	SaveLoad::Variable<VarFileType::I64>("nfa", SLE_OBJECT_ADDRESS(EconomyLabPersistent, nfa)),
	SaveLoad::Variable<VarFileType::I64>("world_gdp_pm", SLE_OBJECT_ADDRESS(EconomyLabPersistent, world_gdp_pm)),
	SaveLoad::Variable<VarFileType::I64>("terms_of_trade_pm", SLE_OBJECT_ADDRESS(EconomyLabPersistent, terms_of_trade_pm)),
	SaveLoad::Variable<VarFileType::I64>("money_anchor", SLE_OBJECT_ADDRESS(EconomyLabPersistent, money_anchor)),
	SaveLoad::Variable<VarFileType::I64>("external_debt", SLE_OBJECT_ADDRESS(EconomyLabPersistent, external_debt)),
	SaveLoad::Variable<VarFileType::I32>("expected_devaluation_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, expected_devaluation_bp)),
	SaveLoad::Variable<VarFileType::I64>("world_growth_remainder", SLE_OBJECT_ADDRESS(EconomyLabPersistent, world_growth_remainder)),
	SaveLoad::Variable<VarFileType::I64>("wages", SLE_OBJECT_ADDRESS(EconomyLabPersistent, wages)),
	SaveLoad::Variable<VarFileType::I64>("dividends", SLE_OBJECT_ADDRESS(EconomyLabPersistent, dividends)),
	SaveLoad::Variable<VarFileType::I64>("worker_consumption", SLE_OBJECT_ADDRESS(EconomyLabPersistent, worker_consumption)),
	SaveLoad::Variable<VarFileType::I64>("owner_consumption", SLE_OBJECT_ADDRESS(EconomyLabPersistent, owner_consumption)),
	SaveLoad::Variable<VarFileType::I64>("household_saving", SLE_OBJECT_ADDRESS(EconomyLabPersistent, household_saving)),
	SaveLoad::Variable<VarFileType::I64>("portfolio_money", SLE_OBJECT_ADDRESS(EconomyLabPersistent, portfolio_money)),
	SaveLoad::Variable<VarFileType::I64>("portfolio_private", SLE_OBJECT_ADDRESS(EconomyLabPersistent, portfolio_private)),
	SaveLoad::Variable<VarFileType::I64>("portfolio_public", SLE_OBJECT_ADDRESS(EconomyLabPersistent, portfolio_public)),
	SaveLoad::Variable<VarFileType::I64>("portfolio_external", SLE_OBJECT_ADDRESS(EconomyLabPersistent, portfolio_external)),
	SaveLoad::Variable<VarFileType::I64>("funding_gap", SLE_OBJECT_ADDRESS(EconomyLabPersistent, funding_gap)),
	SaveLoad::Variable<VarFileType::I32>("parity_pressure_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, parity_pressure_bp)),
	SaveLoad::Variable<VarFileType::I32>("lab_tick", SLE_OBJECT_ADDRESS(EconomyLabPersistent, lab_tick)),
	SaveLoad::Variable<VarFileType::I64>("session_y", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_y)),
	SaveLoad::Variable<VarFileType::I64>("session_tax", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_tax)),
	SaveLoad::Variable<VarFileType::I64>("session_debt", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_debt)),
	SaveLoad::Variable<VarFileType::I64>("session_money", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_money)),
	SaveLoad::Variable<VarFileType::I64>("session_price", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_price)),
	SaveLoad::Variable<VarFileType::I64>("session_exchange", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_exchange)),
	SaveLoad::Variable<VarFileType::I32>("session_rate_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_rate_bp)),
	SaveLoad::Variable<VarFileType::I32>("session_pd_bp", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_pd_bp)),
	SaveLoad::Variable<VarFileType::I32>("session_lgd", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_lgd)),
	SaveLoad::Variable<VarFileType::I64>("session_loans", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_loans)),
	SaveLoad::Variable<VarFileType::I64>("session_reserves", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_reserves)),
	SaveLoad::Variable<VarFileType::I64>("session_bonds", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_bonds)),
	SaveLoad::Variable<VarFileType::I64>("session_deposits", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_deposits)),
	SaveLoad::Variable<VarFileType::I64>("session_equity", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_equity)),
	SaveLoad::Variable<VarFileType::I64>("session_provision", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_provision)),
	SaveLoad::Variable<VarFileType::I32>("session_clamps", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_clamps)),
	SaveLoad::Variable<VarFileType::U8>("session_ok", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_ok)),
	SaveLoad::Variable<VarFileType::I64>("government_extra", SLE_OBJECT_ADDRESS(EconomyLabPersistent, government_extra)),
	SaveLoad::Variable<VarFileType::I64>("import_extra", SLE_OBJECT_ADDRESS(EconomyLabPersistent, import_extra)),
	SaveLoad::Variable<VarFileType::U64>("stream_shock", SLE_OBJECT_ADDRESS(EconomyLabPersistent, stream_shock)),
	SaveLoad::Variable<VarFileType::U64>("stream_trade", SLE_OBJECT_ADDRESS(EconomyLabPersistent, stream_trade)),
	SaveLoad::Variable<VarFileType::U64>("stream_bank", SLE_OBJECT_ADDRESS(EconomyLabPersistent, stream_bank)),
	SaveLoad::Variable<VarFileType::U64>("session_watermark", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_watermark)),
	SaveLoad::Variable<VarFileType::I64>("session_debits", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_debits)),
	SaveLoad::Variable<VarFileType::I64>("session_credits", SLE_OBJECT_ADDRESS(EconomyLabPersistent, session_credits)),
	SaveLoad::Variable<VarFileType::U8>("pending_count", SLE_OBJECT_ADDRESS(EconomyLabPersistent, pending_count)),
	SaveLoad::Variable<VarFileType::U8>("applied_count", SLE_OBJECT_ADDRESS(EconomyLabPersistent, applied_count)),
	SaveLoad::Array<VarFileType::U64, 8>("pending_id", SLE_OBJECT_ADDRESS(EconomyLabPersistent, pending_id)),
	SaveLoad::Array<VarFileType::I32, 8>("pending_tick", SLE_OBJECT_ADDRESS(EconomyLabPersistent, pending_tick)),
	SaveLoad::Array<VarFileType::I64, 8>("pending_magnitude", SLE_OBJECT_ADDRESS(EconomyLabPersistent, pending_magnitude)),
	SaveLoad::Array<VarFileType::U8, 8>("pending_variable", SLE_OBJECT_ADDRESS(EconomyLabPersistent, pending_variable)),
	SaveLoad::Array<VarFileType::U8, 8>("pending_operation", SLE_OBJECT_ADDRESS(EconomyLabPersistent, pending_operation)),
	SaveLoad::Array<VarFileType::U64, 8>("applied_id", SLE_OBJECT_ADDRESS(EconomyLabPersistent, applied_id)),
	SaveLoad::Array<VarFileType::I32, 8>("applied_tick", SLE_OBJECT_ADDRESS(EconomyLabPersistent, applied_tick)),
	SaveLoad::Array<VarFileType::I64, 8>("applied_magnitude", SLE_OBJECT_ADDRESS(EconomyLabPersistent, applied_magnitude)),
	SaveLoad::Array<VarFileType::U8, 8>("applied_variable", SLE_OBJECT_ADDRESS(EconomyLabPersistent, applied_variable)),
	SaveLoad::Array<VarFileType::U8, 8>("applied_operation", SLE_OBJECT_ADDRESS(EconomyLabPersistent, applied_operation)),
	SaveLoad::Variable<VarFileType::U8>("policy_shock_count", SLE_OBJECT_ADDRESS(EconomyLabPersistent, policy_shock_count)),
	SaveLoad::Array<VarFileType::U64, 16>("policy_shock_id", SLE_OBJECT_ADDRESS(EconomyLabPersistent, policy_shock_id)),
	SaveLoad::Array<VarFileType::I32, 16>("policy_shock_tick", SLE_OBJECT_ADDRESS(EconomyLabPersistent, policy_shock_tick)),
	SaveLoad::Array<VarFileType::I64, 16>("policy_shock_magnitude", SLE_OBJECT_ADDRESS(EconomyLabPersistent, policy_shock_magnitude)),
	SaveLoad::Array<VarFileType::U8, 16>("policy_shock_variable", SLE_OBJECT_ADDRESS(EconomyLabPersistent, policy_shock_variable)),
	SaveLoad::Array<VarFileType::I64, 64>("delivered", SLE_OBJECT_ADDRESS(EconomyLabPersistent, delivered)),
};

static const SaveLoadCompat _economylab_sl_compat[] = {
	SLC_VAR("schema"),
	SLC_VAR("generation"),
	SLC_VAR("anchored"),
	SLC_VAR("sequence"),
	SLC_VAR("household_nominal"),
	SLC_VAR("freight_nominal"),
	SLC_VAR("subsidy_nominal"),
	SLC_VAR("potential_base"),
	SLC_VAR("population_base"),
	SLC_VAR("capacity_base"),
	SLC_VAR("capital_base"),
	SLC_VAR("price_base"),
	SLC_VAR("exchange_pm"),
	SLC_VAR("fundamental_pm"),
	SLC_VAR("exports"),
	SLC_VAR("imports"),
	SLC_VAR("world_rate_bp"),
	SLC_VAR("risk_bp"),
	SLC_VAR("loan_rate_bp"),
	SLC_VAR("bank_loans"),
	SLC_VAR("bank_deposits"),
	SLC_VAR("bank_reserves"),
	SLC_VAR("bank_bonds"),
	SLC_VAR("bank_equity"),
	SLC_VAR("employment"),
	SLC_VAR("labour_force"),
	SLC_VAR("output_level"),
	SLC_VAR("ledger_watermark"),
	SLC_VAR("ledger_debits"),
	SLC_VAR("ledger_credits"),
	SLC_VAR("opening_booked"),
	SLC_VAR("treasury_debt"),
	SLC_VAR("cb_bonds"),
	SLC_VAR("cb_reserve_liability"),
	SLC_VAR("official_reserves"),
	SLC_VAR("base_money"),
	SLC_VAR("household_bonds"),
	SLC_VAR("nfa"),
	SLC_VAR("world_gdp_pm"),
	SLC_VAR("terms_of_trade_pm"),
	SLC_VAR("money_anchor"),
	SLC_VAR("external_debt"),
	SLC_VAR("expected_devaluation_bp"),
	SLC_VAR("world_growth_remainder"),
	SLC_VAR("wages"),
	SLC_VAR("dividends"),
	SLC_VAR("worker_consumption"),
	SLC_VAR("owner_consumption"),
	SLC_VAR("household_saving"),
	SLC_VAR("portfolio_money"),
	SLC_VAR("portfolio_private"),
	SLC_VAR("portfolio_public"),
	SLC_VAR("portfolio_external"),
	SLC_VAR("funding_gap"),
	SLC_VAR("parity_pressure_bp"),
	SLC_VAR("lab_tick"),
	SLC_VAR("session_y"),
	SLC_VAR("session_tax"),
	SLC_VAR("session_debt"),
	SLC_VAR("session_money"),
	SLC_VAR("session_price"),
	SLC_VAR("session_exchange"),
	SLC_VAR("session_rate_bp"),
	SLC_VAR("session_pd_bp"),
	SLC_VAR("session_lgd"),
	SLC_VAR("session_loans"),
	SLC_VAR("session_reserves"),
	SLC_VAR("session_bonds"),
	SLC_VAR("session_deposits"),
	SLC_VAR("session_equity"),
	SLC_VAR("session_provision"),
	SLC_VAR("session_clamps"),
	SLC_VAR("session_ok"),
	SLC_VAR("government_extra"),
	SLC_VAR("import_extra"),
	SLC_VAR("stream_shock"),
	SLC_VAR("stream_trade"),
	SLC_VAR("stream_bank"),
	SLC_VAR("session_watermark"),
	SLC_VAR("session_debits"),
	SLC_VAR("session_credits"),
	SLC_VAR("pending_count"),
	SLC_VAR("applied_count"),
	SLC_VAR("pending_id"),
	SLC_VAR("pending_tick"),
	SLC_VAR("pending_magnitude"),
	SLC_VAR("pending_variable"),
	SLC_VAR("pending_operation"),
	SLC_VAR("applied_id"),
	SLC_VAR("applied_tick"),
	SLC_VAR("applied_magnitude"),
	SLC_VAR("applied_variable"),
	SLC_VAR("applied_operation"),
	SLC_VAR("policy_shock_count"),
	SLC_VAR("policy_shock_id"),
	SLC_VAR("policy_shock_tick"),
	SLC_VAR("policy_shock_magnitude"),
	SLC_VAR("policy_shock_variable"),
	SLC_VAR("delivered"),
};

struct ELABChunkHandler : ChunkHandler {
	ELABChunkHandler() : ChunkHandler("ELAB", ChunkType::Table) {}

	void Save() const override
	{
		SlTableHeader(_economylab_desc);
		SlSetArrayIndex(0);
		SlObject(&EconomyLabPersistentState(), _economylab_desc);
	}

	void Load() const override
	{
		/* Missing columns stay at zero: old saves have no watermark and no opening balance sheet. */
		EconomyLabPersistent &loaded = EconomyLabPersistentState();
		loaded.output_level = 0;
		loaded.ledger_watermark = 0;
		loaded.ledger_debits = 0;
		loaded.ledger_credits = 0;
		loaded.opening_booked = 0;
		loaded.treasury_debt = 0;
		loaded.cb_bonds = 0;
		loaded.cb_reserve_liability = 0;
		loaded.official_reserves = 0;
		loaded.base_money = 0;
		loaded.household_bonds = 0;
		loaded.nfa = 0;
		loaded.world_gdp_pm = 10000;
		loaded.terms_of_trade_pm = 10000;
		loaded.money_anchor = 0;
		loaded.external_debt = 0;
		loaded.expected_devaluation_bp = 0;
		loaded.world_growth_remainder = 0;
		loaded.wages = 0;
		loaded.dividends = 0;
		loaded.worker_consumption = 0;
		loaded.owner_consumption = 0;
		loaded.household_saving = 0;
		loaded.portfolio_money = 0;
		loaded.portfolio_private = 0;
		loaded.portfolio_public = 0;
		loaded.portfolio_external = 0;
		loaded.funding_gap = 0;
		loaded.parity_pressure_bp = 0;
		EconomyLabRestoreFreshSession();
		const std::vector<SaveLoad> slt = SlCompatTableHeader(_economylab_desc, _economylab_sl_compat);
		if (!IsSavegameVersionBefore(SaveLoadVersion::RiffToArray) && SlIterateArray() == -1) return;
		SlObject(&EconomyLabPersistentState(), slt);
		if (!IsSavegameVersionBefore(SaveLoadVersion::RiffToArray) && SlIterateArray() != -1) SlErrorCorrupt("Too many ELAB entries");
	}
};

static const CAPYChunkHandler CAPY;
static const PRICChunkHandler PRIC;
static const CAPRChunkHandler CAPR;
static const ECMYChunkHandler ECMY;
static const ELABChunkHandler ELAB;
static const ChunkHandlerRef economy_chunk_handlers[] = {
	CAPY,
	PRIC,
	CAPR,
	ECMY,
	ELAB,
};

extern const ChunkHandlerTable _economy_chunk_handlers(economy_chunk_handlers);
