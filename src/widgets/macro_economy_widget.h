/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy_widget.h Widgets of the IS-LM policy window. */

#ifndef WIDGETS_MACRO_ECONOMY_WIDGET_H
#define WIDGETS_MACRO_ECONOMY_WIDGET_H

/** Widgets of the #MacroEconomyWindow class. */
enum MacroEconomyWidgets : WidgetID {
	WID_MACRO_PANEL,
	WID_MACRO_G_DOWN,
	WID_MACRO_G_UP,
	WID_MACRO_TAX_DOWN,
	WID_MACRO_TAX_UP,
	WID_MACRO_T0_DOWN,
	WID_MACRO_T0_UP,
	WID_MACRO_M_DOWN,
	WID_MACRO_M_UP,
	WID_MACRO_S_DOWN,
	WID_MACRO_S_UP,
	WID_MACRO_SETTINGS,
	WID_MACRO_DASHBOARD,
	WID_MACRO_WORLD,
};

/** Widgets of the #MacroDashboardWindow class. */
enum MacroDashboardWidgets : WidgetID {
	WID_MDASH_PANEL,
	WID_MDASH_SPEED1,
	WID_MDASH_SPEED10,
	WID_MDASH_SPEED100,
	WID_MDASH_SPEED1000,
	WID_MDASH_FLOW,
	WID_MDASH_ACTIVITY,
	WID_MDASH_MONEY,
	WID_MDASH_ISLM,
	WID_MDASH_ASAD,
	WID_MDASH_LEDGER,
};

/** Widgets of the rest-of-the-world window. */
enum MacroWorldWidgets : WidgetID {
	WID_WORLD_PANEL,
	WID_WORLD_GROWTH_DOWN,
	WID_WORLD_GROWTH_UP,
	WID_WORLD_RATE_DOWN,
	WID_WORLD_RATE_UP,
};

/** Widgets of the IS-LM diagram, the AS-AD diagram and the variable ledger. */
enum MacroDiagramWidgets : WidgetID {
	WID_MDIAG_PANEL,
	WID_MDIAG_SCROLL,
};

#endif /* WIDGETS_MACRO_ECONOMY_WIDGET_H */
