/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy_gui.cpp Window for fiscal and monetary policy. */

#include "stdafx.h"

#include "window_gui.h"
#include "strings_func.h"
#include "gui.h"
#include "settings_internal.h"
#include "economy_func.h"
#include "macro_economy.h"
#include "economylab/accounts.h"
#include "network/network.h"
#include "gfx_func.h"
#include "graph_gui.h"
#include "palette_func.h"
#include "timer/timer.h"
#include "timer/timer_game_economy.h"

#include "widgets/macro_economy_widget.h"

#include "table/strings.h"

#include "safeguards.h"

static std::string FormatHundredthsPercent(int32_t hundredths)
{
	if (hundredths < 0) return GetString(STR_MACRO_HUNDREDTHS_NEGATIVE, -hundredths, 2);
	return GetString(STR_MACRO_HUNDREDTHS, hundredths, 2);
}

/** Move an integer setting by a percentage of its current value. A result of zero still moves by one. The click is a shock with an effective tick. */
static void StepEconomySettingPercent(const char *name, int percent, uint8_t shock_variable, bool record = true)
{
	const SettingDesc *setting = GetSettingFromName(name);
	if (setting == nullptr || !setting->IsIntSetting()) return;

	const IntSettingDesc *integer_setting = setting->AsIntSetting();
	const int32_t current = integer_setting->Read(&GetGameSettings());
	const int32_t magnitude = current < 0 ? -current : current;
	const int abs_percent = percent < 0 ? -percent : percent;
	int32_t step = static_cast<int32_t>((static_cast<int64_t>(magnitude) * abs_percent + 50) / 100);
	if (step < 1) step = 1;
	if (percent < 0) step = -step;
	SetSettingValue(integer_setting, current + step);
	const int32_t applied = integer_setting->Read(&GetGameSettings());
	if (record && applied != current) EconomyLabRecordPolicyShock(shock_variable, TimerGameEconomy::date.base(), static_cast<int64_t>(applied) - current);
}

struct MacroEconomyWindow : Window {
	MacroEconomyWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		RefreshMacroEconomy();
	}

	void OnPaint() override
	{
		const bool locked = _networking && !_network_server;
		this->SetWidgetsDisabledState(locked,
			WID_MACRO_G_DOWN, WID_MACRO_G_UP,
			WID_MACRO_TAX_DOWN, WID_MACRO_TAX_UP,
			WID_MACRO_T0_DOWN, WID_MACRO_T0_UP,
			WID_MACRO_M_DOWN, WID_MACRO_M_UP,
			WID_MACRO_S_DOWN, WID_MACRO_S_UP);
		MacroEconomyObserve();
		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MACRO_PANEL) return;

		const EconomySettings &cfg = _settings_game.economy;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Normal);
		int y = tr.top;
		auto row = [&](const std::string &text) {
			if (y + line <= tr.bottom) DrawString(tr.left, tr.right, y, text);
			y += line;
		};

		if (!cfg.islm_enabled) {
			row(GetString(STR_MACRO_DISABLED));
		} else if (_economy.macro_ok == 0) {
			row(GetString(STR_MACRO_NO_EQUILIBRIUM));
		} else {
			const int32_t potential = _economy.macro_natural_output > 0 ? _economy.macro_natural_output : cfg.islm_natural_output;
			row(GetString(STR_MACRO_OUTPUT, _economy.macro_output, potential, FormatHundredthsPercent(_economy.macro_output_gap_bp)));
			row(GetString(STR_MACRO_RATES, FormatHundredthsPercent(_economy.macro_interest_bp), FormatHundredthsPercent(_economy.macro_inflation_bp), FormatHundredthsPercent(_economy.macro_expected_inflation_bp)));
			row(GetString(STR_MACRO_PRICES, _economy.macro_price_index, 2, _economy.macro_real_money));
			const int32_t government = _economy.macro_government;
			const int32_t absorption = _economy.macro_output - _economy.macro_exogenous_output;
			row(GetString(STR_MACRO_EQ_SPLIT, _economy.macro_output, absorption, _economy.macro_exogenous_output));
			row(GetString(STR_MACRO_DEMAND, _economy.macro_consumption, _economy.macro_investment, government, _economy.macro_tax_revenue));
			const EconomyLabOpen &external = EconomyLabOpenState();
			row(GetString(STR_MACRO_TRADE, external.exports, external.imports, external.exports - external.imports));
			const int32_t deficit = government - _economy.macro_tax_revenue;
			row(GetString(STR_MACRO_DEFICIT, deficit, MacroActivityPermyriad(), 2));
			row(GetString(STR_MACRO_LABOUR, _economy.macro_population, FormatHundredthsPercent(_economy.macro_population_growth_bp), FormatHundredthsPercent(_economy.macro_unemployment_bp)));
			row(GetString(STR_MACRO_CAPACITY, _economy.macro_cargo_capacity, _economy.macro_company_investment, _economy.macro_capital));
			row(GetString(STR_MACRO_DEBT, _economy.macro_debt, _economy.macro_debt_service));
			row(GetString(STR_MACRO_AUDIT, _economy.macro_exogenous_output, MacroCalibrationError(), MacroClampCount(), MacroRunQuality()));
		}

		y += line / 2;
		const int64_t policy_scale = EconomyLabPersistentState().potential_base;
		int64_t government_pounds = _economy.macro_government;
		if (policy_scale > 0) government_pounds = static_cast<int64_t>(cfg.islm_government_spending) * policy_scale / 1000;
		if (government_pounds > 2000000000) government_pounds = 2000000000;
		if (government_pounds < 0) government_pounds = 0;
		row(GetString(STR_MACRO_INSTRUMENTS, cfg.islm_government_spending, government_pounds, cfg.islm_tax_rate_percent, cfg.islm_autonomous_tax, cfg.islm_money_supply));
		row(GetString(STR_MACRO_SUBSIDY_LINE, cfg.islm_subsidies, _economy.macro_subsidy_game));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MACRO_G_DOWN: StepEconomySettingPercent("economy.islm_government_spending", -10, ECONOMYLAB_SHOCK_GOVERNMENT); break;
			case WID_MACRO_G_UP: StepEconomySettingPercent("economy.islm_government_spending", 10, ECONOMYLAB_SHOCK_GOVERNMENT); break;
			case WID_MACRO_TAX_DOWN: StepEconomySettingPercent("economy.islm_tax_rate_percent", -10, ECONOMYLAB_SHOCK_TAX); break;
			case WID_MACRO_TAX_UP: StepEconomySettingPercent("economy.islm_tax_rate_percent", 10, ECONOMYLAB_SHOCK_TAX); break;
			case WID_MACRO_T0_DOWN: StepEconomySettingPercent("economy.islm_autonomous_tax", -10, ECONOMYLAB_SHOCK_AUTONOMOUS_TAX); break;
			case WID_MACRO_T0_UP: StepEconomySettingPercent("economy.islm_autonomous_tax", 10, ECONOMYLAB_SHOCK_AUTONOMOUS_TAX); break;
			case WID_MACRO_M_DOWN: StepEconomySettingPercent("economy.islm_money_supply", -10, ECONOMYLAB_SHOCK_MONEY); break;
			case WID_MACRO_M_UP: StepEconomySettingPercent("economy.islm_money_supply", 10, ECONOMYLAB_SHOCK_MONEY); break;
			case WID_MACRO_S_DOWN: StepEconomySettingPercent("economy.islm_subsidies", -10, ECONOMYLAB_SHOCK_SUBSIDIES); break;
			case WID_MACRO_S_UP: StepEconomySettingPercent("economy.islm_subsidies", 10, ECONOMYLAB_SHOCK_SUBSIDIES); break;
			case WID_MACRO_SETTINGS: ShowGameOptions(); break;
			case WID_MACRO_DASHBOARD: ShowMacroDashboard(); break;
			case WID_MACRO_WORLD: ShowMacroWorld(); break;
			default: break;
		}
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_economy_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_ECONOMY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MACRO_PANEL), SetMinimalSize(640, 216), SetFill(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_G_DOWN), SetStringTip(STR_MACRO_G_DOWN, STR_MACRO_G_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_G_UP), SetStringTip(STR_MACRO_G_UP, STR_MACRO_G_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_TAX_DOWN), SetStringTip(STR_MACRO_TAX_DOWN, STR_MACRO_TAX_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_TAX_UP), SetStringTip(STR_MACRO_TAX_UP, STR_MACRO_TAX_TOOLTIP), SetFill(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_T0_DOWN), SetStringTip(STR_MACRO_T0_DOWN, STR_MACRO_T0_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_T0_UP), SetStringTip(STR_MACRO_T0_UP, STR_MACRO_T0_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_M_DOWN), SetStringTip(STR_MACRO_M_DOWN, STR_MACRO_M_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_M_UP), SetStringTip(STR_MACRO_M_UP, STR_MACRO_M_TOOLTIP), SetFill(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_S_DOWN), SetStringTip(STR_MACRO_S_DOWN, STR_MACRO_S_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_S_UP), SetStringTip(STR_MACRO_S_UP, STR_MACRO_S_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_DASHBOARD), SetStringTip(STR_MACRO_OPEN_DASHBOARD, STR_MACRO_OPEN_DASHBOARD_TOOLTIP), SetFill(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_SETTINGS), SetStringTip(STR_MACRO_OPEN_SETTINGS, STR_MACRO_OPEN_SETTINGS_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_WORLD), SetStringTip(STR_MACRO_OPEN_WORLD, STR_MACRO_OPEN_WORLD_TOOLTIP), SetFill(1, 0),
	EndContainer(),
};

static WindowDesc _macro_economy_desc(
	WindowPosition::Center, "macro_economy", 680, 360,
	WindowClass::MacroEconomy, WindowClass::None,
	{},
	_nested_macro_economy_widgets
);

void ShowMacroEconomy()
{
	AllocateWindowDescFront<MacroEconomyWindow>(_macro_economy_desc, 0);
}

/** Share of the normal loan ceiling still open at the current interest rate. */
static int LoanCeilingPercent()
{
	const int rate = std::max<int>(_economy.macro_interest_bp, 0) / 100;
	return 100 * 100 / (100 + 4 * rate);
}

struct MacroDashboardWindow : Window {
	MacroDashboardWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		RefreshMacroEconomy();
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		RefreshMacroEconomy();
	}};

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDASH_PANEL) return;

		const EconomySettings &cfg = _settings_game.economy;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Normal);
		int y = tr.top;
		auto row = [&](const std::string &text) {
			if (y + line <= tr.bottom) DrawString(tr.left, tr.right, y, text);
			y += line;
		};

		MacroEconomyObserve();
		if (!cfg.islm_enabled) {
			row(GetString(STR_MACRO_DISABLED));
			return;
		}
		if (_economy.macro_ok == 0) {
			row(GetString(STR_MACRO_NO_EQUILIBRIUM));
			return;
		}

		const int32_t government = _economy.macro_government;
		row(GetString(STR_MACRO_EQ_OUTPUT, _economy.macro_output, _economy.macro_consumption, _economy.macro_investment, government));
		row(GetString(STR_MACRO_EQ_SPLIT, _economy.macro_output, _economy.macro_output - _economy.macro_exogenous_output, _economy.macro_exogenous_output));
		row(GetString(STR_MACRO_DEFICIT, government - _economy.macro_tax_revenue, MacroActivityPermyriad(), 2));
		row(GetString(STR_MACRO_EQ_CARGO, _economy.macro_cargo_capacity, _economy.macro_cargo_potential, _economy.macro_natural_output));
		row(GetString(STR_MACRO_EQ_RATES, FormatHundredthsPercent(_economy.macro_interest_bp), FormatHundredthsPercent(_economy.macro_inflation_bp), FormatHundredthsPercent(_economy.macro_unemployment_bp)));
		row(GetString(STR_MACRO_EQ_PUBLIC, _economy.macro_debt, _economy.macro_bond_issue, _economy.macro_bond_buyback, _economy.macro_debt_service));
		row(GetString(STR_MACRO_EQ_PRIVATE, _economy.macro_private_debt, _economy.macro_private_debt_delta, _economy.macro_private_interest, LoanCeilingPercent()));
		y += line / 2;
		row(GetString(STR_MACRO_LINK_INVESTMENT));
		row(GetString(STR_MACRO_LINK_DEBT));
		row(GetString(STR_MACRO_LINK_CARGO));
		row(GetString(STR_MACRO_LINK_SUBSIDY));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MDASH_SPEED1: SetGameSpeed(100); break;
			case WID_MDASH_SPEED10: SetGameSpeed(1000); break;
			case WID_MDASH_SPEED100: SetGameSpeed(10000); break;
			case WID_MDASH_SPEED1000: SetGameSpeed(100000); break;
			case WID_MDASH_FLOW: ShowMacroSingleGraph(MACRO_HIST_INFLATION); break;
			case WID_MDASH_ACTIVITY: ShowMacroSingleGraph(MACRO_HIST_OUTPUT); break;
			case WID_MDASH_MONEY: ShowMacroSingleGraph(MACRO_HIST2_INTEREST); break;
			case WID_MDASH_ISLM: ShowMacroIsLmDiagram(); break;
			case WID_MDASH_ASAD: ShowMacroAsAdDiagram(); break;
			case WID_MDASH_LEDGER: ShowMacroLedger(); break;
			default: break;
		}
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_dashboard_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_DASHBOARD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MDASH_PANEL), SetMinimalSize(760, 250), SetFill(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_SPEED1), SetStringTip(STR_MACRO_SPEED_1, STR_MACRO_SPEED_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_SPEED10), SetStringTip(STR_MACRO_SPEED_10, STR_MACRO_SPEED_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_SPEED100), SetStringTip(STR_MACRO_SPEED_100, STR_MACRO_SPEED_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_SPEED1000), SetStringTip(STR_MACRO_SPEED_1000, STR_MACRO_SPEED_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_FLOW), SetStringTip(STR_MACRO_OPEN_FLOW_GRAPH, STR_MACRO_OPEN_FLOW_GRAPH_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_ACTIVITY), SetStringTip(STR_MACRO_OPEN_ACTIVITY_GRAPH, STR_MACRO_OPEN_ACTIVITY_GRAPH_TOOLTIP), SetFill(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_MONEY), SetStringTip(STR_MACRO_OPEN_MONEY_GRAPH, STR_MACRO_OPEN_MONEY_GRAPH_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_ISLM), SetStringTip(STR_MACRO_OPEN_ISLM, STR_MACRO_OPEN_ISLM_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_ASAD), SetStringTip(STR_MACRO_OPEN_ASAD, STR_MACRO_OPEN_ASAD_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MDASH_LEDGER), SetStringTip(STR_MACRO_OPEN_LEDGER, STR_MACRO_OPEN_LEDGER_TOOLTIP), SetFill(1, 0),
	EndContainer(),
};

static WindowDesc _macro_dashboard_desc(
	WindowPosition::Center, "macro_dashboard", 820, 380,
	WindowClass::MacroDashboard, WindowClass::None,
	{},
	_nested_macro_dashboard_widgets
);

void ShowMacroDashboard()
{
	AllocateWindowDescFront<MacroDashboardWindow>(_macro_dashboard_desc, 0);
}

struct MacroWorldWindow : Window {
	MacroWorldWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		RefreshMacroEconomy();
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		this->SetDirty();
	}};

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_WORLD_PANEL) return;
		const EconomyLabOpen &open = EconomyLabOpenState();
		const EconomySettings &cfg = _settings_game.economy;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Normal);
		int y = tr.top;
		auto row = [&](const std::string &text) {
			if (y + line <= tr.bottom) DrawString(tr.left, tr.right, y, text);
			y += line;
		};
		row(GetString(STR_MACRO_WORLD_GROWTH, FormatHundredthsPercent(cfg.islm_world_growth_bp), open.world_gdp_pm));
		row(GetString(STR_MACRO_WORLD_RATE, FormatHundredthsPercent(cfg.islm_world_interest_bp)));
		row(GetString(STR_MACRO_WORLD_DOLLAR, open.exchange_pm, FormatHundredthsPercent(open.expected_devaluation_bp)));
		row(GetString(STR_MACRO_WORLD_TRADE, open.exports, open.imports, open.terms_of_trade_pm));
		row(GetString(STR_MACRO_WORLD_DEBT, open.external_debt, open.external_service));
		y += line / 2;
		row(GetString(STR_MACRO_WORLD_NOTE));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_WORLD_GROWTH_DOWN: StepEconomySettingPercent("economy.islm_world_growth_bp", -10, ECONOMYLAB_SHOCK_WORLDRATE, false); break;
			case WID_WORLD_GROWTH_UP: StepEconomySettingPercent("economy.islm_world_growth_bp", 10, ECONOMYLAB_SHOCK_WORLDRATE, false); break;
			case WID_WORLD_RATE_DOWN: StepEconomySettingPercent("economy.islm_world_interest_bp", -10, ECONOMYLAB_SHOCK_WORLDRATE); break;
			case WID_WORLD_RATE_UP: StepEconomySettingPercent("economy.islm_world_interest_bp", 10, ECONOMYLAB_SHOCK_WORLDRATE); break;
			default: break;
		}
		this->SetDirty();
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_world_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_WORLD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_WORLD_PANEL), SetMinimalSize(640, 140), SetFill(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_WORLD_GROWTH_DOWN), SetStringTip(STR_MACRO_WORLD_GROWTH_DOWN, STR_MACRO_WORLD_GROWTH_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_WORLD_GROWTH_UP), SetStringTip(STR_MACRO_WORLD_GROWTH_UP, STR_MACRO_WORLD_GROWTH_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_WORLD_RATE_DOWN), SetStringTip(STR_MACRO_WORLD_RATE_DOWN, STR_MACRO_WORLD_RATE_TOOLTIP), SetFill(1, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_WORLD_RATE_UP), SetStringTip(STR_MACRO_WORLD_RATE_UP, STR_MACRO_WORLD_RATE_TOOLTIP), SetFill(1, 0),
	EndContainer(),
};

static WindowDesc _macro_world_desc(
	WindowPosition::Center, "macro_world", 680, 240,
	WindowClass::MacroWorld, WindowClass::None,
	{},
	_nested_macro_world_widgets
);

void ShowMacroWorld()
{
	AllocateWindowDescFront<MacroWorldWindow>(_macro_world_desc, 0);
}

/** Map a value into a pixel span. A zero span stays on the origin. */
static int MapAxis(int origin, int span, int32_t value, int32_t low, int32_t high, bool from_top)
{
	const int32_t range = std::max<int32_t>(high - low, 1);
	const int along = static_cast<int>((static_cast<int64_t>(value - low) * span) / range);
	return from_top ? origin + span - along : origin + along;
}

static void DrawDiagram(const Rect &plot, int32_t axis_x0, int32_t axis_x1, int32_t axis_y0, int32_t axis_y1, int32_t sample0, int32_t sample1, int samples, auto x_of, auto y_of, PixelColour colour)
{
	int previous_x = 0;
	int previous_y = 0;
	bool has_previous = false;
	for (int i = 0; i <= samples; i++) {
		const int32_t sample = sample0 + static_cast<int32_t>((static_cast<int64_t>(sample1 - sample0) * i) / samples);
		const int x = MapAxis(plot.left, plot.right - plot.left, x_of(sample), axis_x0, axis_x1, false);
		const int y = MapAxis(plot.top, plot.bottom - plot.top, y_of(sample), axis_y0, axis_y1, true);
		if (has_previous) GfxDrawLine(previous_x, previous_y, x, y, colour, 2, 0);
		previous_x = x;
		previous_y = y;
		has_previous = true;
	}
}

static void DrawEquilibrium(const Rect &plot, int32_t x0, int32_t x1, int32_t y0, int32_t y1, int32_t x, int32_t y)
{
	const int px = MapAxis(plot.left, plot.right - plot.left, x, x0, x1, false);
	const int py = MapAxis(plot.top, plot.bottom - plot.top, y, y0, y1, true);
	GfxFillRect(px - 2, py - 2, px + 2, py + 2, GetColourGradient(Colours::Orange, Shade::Lighter));
}

struct MacroIsLmWindow : Window {
	MacroIsLmWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		this->SetDirty();
	}};

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDIAG_PANEL) return;
		const Rect plot = r.Shrink(WidgetDimensions::scaled.framerect);
		const int32_t x1 = std::max<int32_t>({_economy.macro_output, _economy.macro_natural_output, _economy.macro_goods_demand, 100}) * 3 / 2;
		const int32_t y0 = std::min<int32_t>(_economy.macro_interest_bp, 0) - 500;
		const int32_t y1 = std::max<int32_t>(_economy.macro_interest_bp, 0) + 500;
		GfxFillRect(plot, GetColourGradient(Colours::Grey, Shade::Dark));
		/* IS is sampled along the interest rate. LM is sampled along output. */
		int prev_x = 0;
		int prev_y = 0;
		bool has_prev = false;
		for (int i = 0; i <= 48; i++) {
			const int32_t rate = y0 + static_cast<int32_t>((static_cast<int64_t>(y1 - y0) * i) / 48);
			const int32_t output = MacroOutputAtInterest(rate);
			const int x = MapAxis(plot.left, plot.right - plot.left, output, 0, x1, false);
			const int y = MapAxis(plot.top, plot.bottom - plot.top, rate, y0, y1, true);
			if (has_prev) GfxDrawLine(prev_x, prev_y, x, y, GetColourGradient(Colours::Blue, Shade::Lighter), 2, 0);
			prev_x = x;
			prev_y = y;
			has_prev = true;
		}
		DrawDiagram(plot, 0, x1, y0, y1, 0, x1, 48,
			[](int32_t output) { return output; },
			[](int32_t output) { return MacroInterestAtOutput(output); },
			GetColourGradient(Colours::Red, Shade::Lighter));
		DrawEquilibrium(plot, 0, x1, y0, y1, _economy.macro_goods_demand != 0 ? _economy.macro_goods_demand : _economy.macro_output, _economy.macro_interest_bp);
		DrawString(plot.left, plot.right, plot.top, STR_MACRO_ISLM_LEGEND, TextColour::White);
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_islm_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_ISLM_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MDIAG_PANEL), SetMinimalSize(520, 280), SetFill(1, 1), EndContainer(),
};

static WindowDesc _macro_islm_desc(
	WindowPosition::Center, "macro_islm", 560, 340,
	WindowClass::MacroIsLmGraph, WindowClass::None,
	{},
	_nested_macro_islm_widgets
);

void ShowMacroIsLmDiagram()
{
	AllocateWindowDescFront<MacroIsLmWindow>(_macro_islm_desc, 0);
}

struct MacroAsAdWindow : Window {
	MacroAsAdWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		this->SetDirty();
	}};

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDIAG_PANEL) return;
		const Rect plot = r.Shrink(WidgetDimensions::scaled.framerect);
		const int32_t x1 = std::max<int32_t>({_economy.macro_output, _economy.macro_natural_output, _economy.macro_goods_demand, 100}) * 3 / 2;
		const int32_t price = std::max<int32_t>(_economy.macro_price_index, 100);
		const int32_t y0 = price / 2;
		const int32_t y1 = price * 2;
		GfxFillRect(plot, GetColourGradient(Colours::Grey, Shade::Dark));
		/* Long-run supply is vertical at potential output. */
		const int x_star = MapAxis(plot.left, plot.right - plot.left, _economy.macro_natural_output, 0, x1, false);
		GfxDrawLine(x_star, plot.top, x_star, plot.bottom, GetColourGradient(Colours::Green, Shade::Lighter), 2, 0);
		DrawDiagram(plot, 0, x1, y0, y1, y0, y1, 48,
			[](int32_t sample) { return MacroOutputAtPrice(sample); },
			[](int32_t sample) { return sample; },
			GetColourGradient(Colours::Blue, Shade::Lighter));
		DrawDiagram(plot, 0, x1, y0, y1, 0, x1, 48,
			[](int32_t output) { return output; },
			[](int32_t output) { return MacroPriceAtOutput(output); },
			GetColourGradient(Colours::Red, Shade::Lighter));
		DrawEquilibrium(plot, 0, x1, y0, y1, _economy.macro_goods_demand != 0 ? _economy.macro_goods_demand : _economy.macro_output, _economy.macro_price_index);
		DrawString(plot.left, plot.right, plot.top, STR_MACRO_ASAD_LEGEND, TextColour::White);
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_asad_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_ASAD_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MDIAG_PANEL), SetMinimalSize(520, 280), SetFill(1, 1), EndContainer(),
};

static WindowDesc _macro_asad_desc(
	WindowPosition::Center, "macro_asad", 560, 340,
	WindowClass::MacroAsAdGraph, WindowClass::None,
	{},
	_nested_macro_asad_widgets
);

void ShowMacroAsAdDiagram()
{
	AllocateWindowDescFront<MacroAsAdWindow>(_macro_asad_desc, 0);
}

static void AppendDigits(std::string &text, int64_t value, bool grouped)
{
	char digits[24];
	int count = 0;
	do {
		digits[count++] = static_cast<char>('0' + static_cast<int>(value % 10));
		value /= 10;
	} while (value > 0 && count < static_cast<int>(std::size(digits)));
	for (int i = count - 1; i >= 0; i--) {
		if (grouped && i != count - 1 && (i + 1) % 3 == 0) text += '.';
		text += digits[i];
	}
}

static std::string FormatMonitorValue(int64_t value, uint8_t format)
{
	std::string text;
	if (value < 0) {
		text += '-';
		value = -value;
	}
	if (format == 1 || format == 2) {
		AppendDigits(text, value / 100, false);
		text += ',';
		const int64_t frac = value % 100;
		text += static_cast<char>('0' + static_cast<int>(frac / 10));
		text += static_cast<char>('0' + static_cast<int>(frac % 10));
		if (format == 1) text += '%';
		return text;
	}
	AppendDigits(text, value, true);
	return text;
}

struct MacroLedgerWindow : Window {
	MacroLedgerWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		this->GetScrollbar(WID_MDIAG_SCROLL)->SetCount(64);
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		this->SetDirty();
	}};

	void OnResize() override
	{
		this->GetScrollbar(WID_MDIAG_SCROLL)->SetCapacityFromWidget(this, WID_MDIAG_PANEL, WidgetDimensions::scaled.framerect.Vertical());
	}

	void OnPaint() override
	{
		MacroMonitorRow rows[64];
		const int count = FillMacroMonitor(rows, 64);
		this->GetScrollbar(WID_MDIAG_SCROLL)->SetCount(count * 2);
		this->DrawWidgets();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDIAG_PANEL) return;
		MacroMonitorRow rows[64];
		const int count = FillMacroMonitor(rows, 64);
		const Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Small);
		const int lines = std::max(1, (tr.bottom - tr.top) / line);
		const int first = static_cast<int>(this->GetScrollbar(WID_MDIAG_SCROLL)->GetPosition());
		int y = tr.top;
		for (int slot = first; slot < count * 2 && y + line <= tr.bottom && (slot - first) < lines; slot++) {
			const MacroMonitorRow &row = rows[slot / 2];
			if (slot % 2 == 0) {
				const std::string text = std::string(row.label) + "  " + FormatMonitorValue(row.value, row.format);
				DrawString(tr.left, tr.right, y, text, TextColour::Black, AlignmentH::Start, false, FontSize::Small);
			} else {
				DrawString(tr.left, tr.right, y, std::string(row.detail), TextColour::Grey, AlignmentH::Start, false, FontSize::Small);
			}
			y += line;
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_ledger_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_LEDGER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_DEFSIZEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, Colours::Brown, WID_MDIAG_PANEL), SetFill(1, 1), SetResize(1, 1),
				SetMinimalTextLines(18, WidgetDimensions::unscaled.framerect.Vertical()), SetScrollbar(WID_MDIAG_SCROLL),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, Colours::Brown, WID_MDIAG_SCROLL),
			NWidget(WWT_RESIZEBOX, Colours::Brown),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _macro_ledger_desc(
	WindowPosition::Center, "macro_ledger", 680, 480,
	WindowClass::MacroLedger, WindowClass::None,
	{},
	_nested_macro_ledger_widgets
);

void ShowMacroLedger()
{
	AllocateWindowDescFront<MacroLedgerWindow>(_macro_ledger_desc, 0);
}

struct MacroSingleWindow : Window {
	MacroSingleWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		this->SetDirty();
	}};

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDIAG_PANEL) return;
		const int series = this->window_number;
		const Rect frame = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Normal);
		DrawString(frame.left, frame.right, frame.top, std::string(MacroSeriesName(series)), TextColour::Black, AlignmentH::Start);
		const int count = MacroHistoryCount();
		if (count < 1) return;
		int32_t low = MacroHistoryValue(series, 0);
		int32_t high = low;
		for (int age = 1; age < count; age++) {
			const int32_t value = MacroHistoryValue(series, age);
			low = std::min(low, value);
			high = std::max(high, value);
		}
		if (high <= low) {
			low -= 1;
			high += 1;
		}
		const Rect plot{frame.left, frame.top + line + 4, frame.right, frame.bottom};
		GfxFillRect(plot, GetColourGradient(Colours::Grey, Shade::Dark));
		int previous_x = 0;
		int previous_y = 0;
		bool has_previous = false;
		for (int i = 0; i < count; i++) {
			const int age = count - 1 - i;
			const int x = MapAxis(plot.left, plot.right - plot.left, i, 0, std::max(count - 1, 1), false);
			const int y = MapAxis(plot.top, plot.bottom - plot.top, MacroHistoryValue(series, age), low, high, true);
			if (has_previous) GfxDrawLine(previous_x, previous_y, x, y, GetColourGradient(Colours::Blue, Shade::Lighter), 2, 0);
			previous_x = x;
			previous_y = y;
			has_previous = true;
		}
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_single_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_GRAPH_ACTIVITY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MDIAG_PANEL), SetMinimalSize(520, 220), SetFill(1, 1), EndContainer(),
};

static WindowDesc _macro_single_desc(
	WindowPosition::Center, "macro_single", 560, 280,
	WindowClass::MacroFlowGraph, WindowClass::None,
	{},
	_nested_macro_single_widgets
);

void ShowMacroSingleGraph(int series)
{
	if (series < 0 || series >= MACRO_HIST_SERIES * 2) return;
	AllocateWindowDescFront<MacroSingleWindow>(_macro_single_desc, series);
}
