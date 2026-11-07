/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy.h Closed-economy IS-LM equilibrium and expectations-augmented Phillips curve. */

#ifndef MACRO_ECONOMY_H
#define MACRO_ECONOMY_H

void ResetMacroEconomy();
void RefreshMacroEconomy();
void MacroEconomyMonthly();

/**
 * Production multiplier implied by the output gap, in permyriad.
 * 10000 leaves town and industry production unchanged.
 */
uint MacroActivityPermyriad();

#endif /* MACRO_ECONOMY_H */
