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

static void StepEconomySetting(const char *name, int32_t delta)
{
	const SettingDesc *setting = GetSettingFromName(name);
	if (setting == nullptr || !setting->IsIntSetting()) return;

	const IntSettingDesc *integer_setting = setting->AsIntSetting();
	const int32_t current = integer_setting->Read(&GetGameSettings());
	SetSettingValue(integer_setting, current + delta);
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
			row(GetString(STR_MACRO_EQ_SPLIT, _economy.macro_output, _economy.macro_game_output, _economy.macro_exogenous_output));
			row(GetString(STR_MACRO_DEMAND, _economy.macro_consumption, _economy.macro_investment, government, _economy.macro_tax_revenue));
			const int32_t deficit = government - _economy.macro_tax_revenue;
			row(GetString(STR_MACRO_DEFICIT, deficit, MacroActivityPermyriad(), 2));
			row(GetString(STR_MACRO_LABOUR, _economy.macro_population, FormatHundredthsPercent(_economy.macro_population_growth_bp), FormatHundredthsPercent(_economy.macro_unemployment_bp)));
			row(GetString(STR_MACRO_CAPACITY, _economy.macro_cargo_capacity, _economy.macro_company_investment, _economy.macro_capital));
			row(GetString(STR_MACRO_DEBT, _economy.macro_debt, _economy.macro_debt_service));
		}

		y += line / 2;
		row(GetString(STR_MACRO_INSTRUMENTS, cfg.islm_government_spending, cfg.islm_tax_rate_percent, cfg.islm_autonomous_tax, cfg.islm_money_supply));
		row(GetString(STR_MACRO_SUBSIDY_LINE, cfg.islm_subsidies, _economy.macro_subsidy_game));
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_MACRO_G_DOWN: StepEconomySetting("economy.islm_government_spending", -10); break;
			case WID_MACRO_G_UP: StepEconomySetting("economy.islm_government_spending", 10); break;
			case WID_MACRO_TAX_DOWN: StepEconomySetting("economy.islm_tax_rate_percent", -1); break;
			case WID_MACRO_TAX_UP: StepEconomySetting("economy.islm_tax_rate_percent", 1); break;
			case WID_MACRO_T0_DOWN: StepEconomySetting("economy.islm_autonomous_tax", -10); break;
			case WID_MACRO_T0_UP: StepEconomySetting("economy.islm_autonomous_tax", 10); break;
			case WID_MACRO_M_DOWN: StepEconomySetting("economy.islm_money_supply", -20); break;
			case WID_MACRO_M_UP: StepEconomySetting("economy.islm_money_supply", 20); break;
			case WID_MACRO_S_DOWN: StepEconomySetting("economy.islm_subsidies", -10); break;
			case WID_MACRO_S_UP: StepEconomySetting("economy.islm_subsidies", 10); break;
			case WID_MACRO_SETTINGS: ShowGameOptions(); break;
			case WID_MACRO_DASHBOARD: ShowMacroDashboard(); break;
			default: break;
		}
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
		row(GetString(STR_MACRO_EQ_SPLIT, _economy.macro_output, _economy.macro_game_output, _economy.macro_exogenous_output));
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
			case WID_MDASH_SPEED1: _game_speed = 100; break;
			case WID_MDASH_SPEED10: _game_speed = 1000; break;
			case WID_MDASH_SPEED100: _game_speed = 10000; break;
			case WID_MDASH_SPEED1000: _game_speed = 100000; break;
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
		const int32_t x1 = std::max<int32_t>({_economy.macro_output, _economy.macro_natural_output, 100}) * 3 / 2;
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
		DrawEquilibrium(plot, 0, x1, y0, y1, _economy.macro_output, _economy.macro_interest_bp);
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
		const int32_t x1 = std::max<int32_t>({_economy.macro_output, _economy.macro_natural_output, 100}) * 3 / 2;
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
		DrawEquilibrium(plot, 0, x1, y0, y1, _economy.macro_output, _economy.macro_price_index);
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

struct MacroLedgerWindow : Window {
	MacroLedgerWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	const IntervalTimer<TimerGameEconomy> daily_refresh{{TimerGameEconomy::Trigger::Day, TimerGameEconomy::Priority::None}, [this](auto) {
		RefreshMacroEconomy();
	}};

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_MDIAG_PANEL) return;
		const EconomySettings &cfg = _settings_game.economy;
		Rect tr = r.Shrink(WidgetDimensions::scaled.framerect);
		const int line = GetCharacterHeight(FontSize::Small);
		int y = tr.top;
		auto row = [&](StringID id, auto... args) {
			if (y + line <= tr.bottom) DrawString(tr.left, tr.right, y, GetString(id, args...), TextColour::Black, AlignmentH::Start, false, FontSize::Small);
			y += line;
		};
		row(STR_MACRO_VAR_OUTPUT, _economy.macro_output);
		row(STR_MACRO_VAR_GAME, _economy.macro_game_output);
		row(STR_MACRO_VAR_EXO, _economy.macro_exogenous_output);
		row(STR_MACRO_VAR_C, _economy.macro_consumption);
		row(STR_MACRO_VAR_I, _economy.macro_investment);
		row(STR_MACRO_VAR_G, _economy.macro_government);
		row(STR_MACRO_VAR_YSTAR, _economy.macro_natural_output);
		row(STR_MACRO_VAR_P, _economy.macro_price_index / 100);
		row(STR_MACRO_VAR_M, cfg.islm_money_supply);
		row(STR_MACRO_VAR_RM, _economy.macro_real_money);
		row(STR_MACRO_VAR_MD, _economy.macro_money_demand);
		row(STR_MACRO_VAR_T, _economy.macro_tax_revenue);
		row(STR_MACRO_VAR_POP, _economy.macro_population);
		row(STR_MACRO_VAR_CARGO, _economy.macro_cargo_capacity);
		row(STR_MACRO_VAR_GSET, cfg.islm_government_spending);
		row(STR_MACRO_VAR_SSET, cfg.islm_subsidies);
		row(STR_MACRO_VAR_TAX, cfg.islm_tax_rate_percent);
		row(STR_MACRO_VAR_T0, cfg.islm_autonomous_tax);
		row(STR_MACRO_VAR_C0, cfg.islm_autonomous_consumption);
		y += line / 2;
		if (_economy.macro_interest_bp < 0) {
			row(STR_MACRO_VAR_R_NEG, -_economy.macro_interest_bp, 2);
		} else {
			row(STR_MACRO_VAR_R, _economy.macro_interest_bp, 2);
		}
		if (_economy.macro_inflation_bp < 0) {
			row(STR_MACRO_VAR_PI_NEG, -_economy.macro_inflation_bp, 2);
		} else {
			row(STR_MACRO_VAR_PI, _economy.macro_inflation_bp, 2);
		}
		row(STR_MACRO_VAR_U, _economy.macro_unemployment_bp, 2);
		row(STR_MACRO_VAR_DEBT_PRICE, _economy.macro_private_debt_price);
	}
};

static constexpr std::initializer_list<NWidgetPart> _nested_macro_ledger_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Brown),
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_LEDGER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, Colours::Brown),
		NWidget(WWT_STICKYBOX, Colours::Brown),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Brown, WID_MDIAG_PANEL), SetMinimalSize(640, 420), SetFill(1, 1), EndContainer(),
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
		NWidget(WWT_CAPTION, Colours::Brown), SetStringTip(STR_MACRO_LEDGER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
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
