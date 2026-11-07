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
	}

	void OnPaint() override
	{
		const bool locked = _networking && !_network_server;
		this->SetWidgetsDisabledState(locked,
			WID_MACRO_G_DOWN, WID_MACRO_G_UP,
			WID_MACRO_TAX_DOWN, WID_MACRO_TAX_UP,
			WID_MACRO_T0_DOWN, WID_MACRO_T0_UP,
			WID_MACRO_M_DOWN, WID_MACRO_M_UP);
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
			row(GetString(STR_MACRO_OUTPUT, _economy.macro_output, cfg.islm_natural_output, FormatHundredthsPercent(_economy.macro_output_gap_bp)));
			row(GetString(STR_MACRO_RATES, FormatHundredthsPercent(_economy.macro_interest_bp), FormatHundredthsPercent(_economy.macro_inflation_bp), FormatHundredthsPercent(_economy.macro_expected_inflation_bp)));
			row(GetString(STR_MACRO_PRICES, _economy.macro_price_index, 2, _economy.macro_real_money));
			row(GetString(STR_MACRO_DEMAND, _economy.macro_consumption, _economy.macro_investment, cfg.islm_government_spending, _economy.macro_tax_revenue));
			const int32_t deficit = static_cast<int32_t>(cfg.islm_government_spending) - _economy.macro_tax_revenue;
			row(GetString(STR_MACRO_DEFICIT, deficit, MacroActivityPermyriad(), 2));
		}

		y += line / 2;
		row(GetString(STR_MACRO_INSTRUMENTS, cfg.islm_government_spending, cfg.islm_tax_rate_percent, cfg.islm_autonomous_tax, cfg.islm_money_supply));
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
			case WID_MACRO_SETTINGS: ShowGameOptions(); break;
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
	NWidget(WWT_PANEL, Colours::Brown, WID_MACRO_PANEL), SetMinimalSize(520, 120), SetFill(1, 1), EndContainer(),
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
		NWidget(WWT_PUSHTXTBTN, Colours::Brown, WID_MACRO_SETTINGS), SetStringTip(STR_MACRO_OPEN_SETTINGS, STR_MACRO_OPEN_SETTINGS_TOOLTIP), SetFill(1, 0),
	EndContainer(),
};

static WindowDesc _macro_economy_desc(
	WindowPosition::Center, "macro_economy", 540, 220,
	WindowClass::MacroEconomy, WindowClass::None,
	{},
	_nested_macro_economy_widgets
);

void ShowMacroEconomy()
{
	AllocateWindowDescFront<MacroEconomyWindow>(_macro_economy_desc, 0);
}
