/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file macro_economy.cpp Closed-economy IS-LM equilibrium and expectations-augmented Phillips curve. */

#include "stdafx.h"

#include "macro_economy.h"
#include "economy_func.h"
#include "genworld.h"
#include "news_func.h"
#include "strings_func.h"
#include "timer/timer_game_economy.h"
#include "window_func.h"

#include "safeguards.h"

/*
 * Closed economy, fixed-point and deterministic (no floating point).
 *
 * Goods market, the IS schedule:
 *   Y = C + I + G
 *   C = C0 + c (Y - T)
 *   T = T0 + t Y
 *   I = I0 - b r
 * which solves as Y = (A - b r) / s, with A = C0 + I0 + G - c T0
 * and s = 1 - c (1 - t).
 *
 * Money market, the LM schedule. The displayed price index uses 1000 as 1,
 * and the saved index keeps two extra digits so monthly inflation is visible:
 *   M / P = k Y - h r
 *
 * Prices, expectations-augmented Phillips curve, annual rates:
 *   pi = pi_e + alpha (Y - Y*) / Y*
 * Adaptive expectations, with lambda as a share per year, advanced one
 * economy month at a time:
 *   pi_e <- pi_e + (lambda / 12) (pi - pi_e)
 *
 * Coefficients c, t, k, alpha and lambda are integer percents. b and h are
 * index points per percentage point of the interest rate. Interest rates and
 * inflation are stored in hundredths of a percent.
 */

static constexpr int64_t MACRO_PRICE_SCALE = 100; ///< Stored price index is the displayed index times this.
static constexpr int64_t MACRO_MIN_PRICE = 100 * MACRO_PRICE_SCALE;
static constexpr int64_t MACRO_MAX_PRICE = 200000 * MACRO_PRICE_SCALE;
static constexpr int64_t MACRO_MIN_LEAKAGE = 100; ///< s * 10000, at least one percent of income leaks out of the goods market.

struct IsLmResult {
	bool ok = false;
	int32_t output = 0;
	int32_t interest_bp = 0;
	int32_t consumption = 0;
	int32_t investment = 0;
	int32_t tax_revenue = 0;
	int32_t real_money = 0;
	int32_t output_gap_bp = 0;
};

static int32_t ClampI32(int64_t value, int64_t lower, int64_t upper)
{
	if (value < lower) return static_cast<int32_t>(lower);
	if (value > upper) return static_cast<int32_t>(upper);
	return static_cast<int32_t>(value);
}

static int32_t RoundHundredthsToPercent(int32_t hundredths)
{
	if (hundredths >= 0) return (hundredths + 50) / 100;
	return -((-hundredths + 50) / 100);
}

/**
 * Solve the IS-LM crossing at the current macro price index.
 * The price index is given, so this is the short-run sticky-price equilibrium.
 */
static IsLmResult SolveIsLm(int32_t price_index)
{
	const EconomySettings &cfg = _settings_game.economy;
	IsLmResult result;

	const int64_t mpc = cfg.islm_mpc_percent;
	const int64_t tax_rate = cfg.islm_tax_rate_percent;
	const int64_t leakage = 10000 - mpc * (100 - tax_rate);
	const int64_t sensitivity = cfg.islm_investment_sensitivity;
	const int64_t money_interest = cfg.islm_money_interest_sensitivity;
	const int64_t money_income = cfg.islm_money_income_percent;
	const int64_t natural_output = std::max<int32_t>(cfg.islm_natural_output, 1);
	if (leakage < MACRO_MIN_LEAKAGE || sensitivity < 1 || money_interest < 1 || money_income < 1) return result;

	/* Displayed index 1000 is stored as 100000, so real balances are M * 1000 / displayed index. */
	const int64_t price = std::clamp<int64_t>(price_index, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
	const int64_t real_money = static_cast<int64_t>(cfg.islm_money_supply) * 1000 * MACRO_PRICE_SCALE / price;
	const int64_t autonomous = static_cast<int64_t>(cfg.islm_autonomous_consumption) + cfg.islm_autonomous_investment + cfg.islm_government_spending - mpc * cfg.islm_autonomous_tax / 100;

	const int64_t numerator = (money_interest * autonomous + sensitivity * real_money) * 1000000;
	const int64_t denominator = 100 * money_interest * leakage + 10000 * sensitivity * money_income;
	if (denominator <= 0) return result;

	const int64_t output = std::clamp<int64_t>(numerator / denominator, 0, 10000000);
	const int64_t interest_bp = (money_income * output - 100 * real_money) / money_interest;
	const int64_t tax_revenue = static_cast<int64_t>(cfg.islm_autonomous_tax) + tax_rate * output / 100;
	const int64_t disposable = output - tax_revenue;
	const int64_t consumption = static_cast<int64_t>(cfg.islm_autonomous_consumption) + mpc * disposable / 100;
	const int64_t investment = static_cast<int64_t>(cfg.islm_autonomous_investment) - sensitivity * interest_bp / 100;

	result.ok = true;
	result.output = ClampI32(output, 0, 10000000);
	result.interest_bp = ClampI32(interest_bp, -5000, 10000);
	result.consumption = ClampI32(consumption, -10000000, 10000000);
	result.investment = ClampI32(investment, -10000000, 10000000);
	result.tax_revenue = ClampI32(tax_revenue, -10000000, 10000000);
	result.real_money = ClampI32(real_money, 0, 10000000);
	result.output_gap_bp = ClampI32((output - natural_output) * 10000 / natural_output, -10000, 100000);
	return result;
}

static int32_t PhillipsInflation(int32_t expected_bp, int32_t output_gap_bp)
{
	const int64_t slope = _settings_game.economy.islm_phillips_slope_percent;
	return ClampI32(static_cast<int64_t>(expected_bp) + slope * output_gap_bp / 100, -5000, 10000);
}

static void StoreEquilibrium(const IsLmResult &result, int32_t inflation_bp)
{
	_economy.macro_ok = result.ok ? 1 : 0;
	if (!result.ok) return;

	_economy.macro_output = result.output;
	_economy.macro_interest_bp = result.interest_bp;
	_economy.macro_output_gap_bp = result.output_gap_bp;
	_economy.macro_consumption = result.consumption;
	_economy.macro_investment = result.investment;
	_economy.macro_tax_revenue = result.tax_revenue;
	_economy.macro_real_money = result.real_money;
	_economy.macro_inflation_bp = inflation_bp;

	const int32_t charged = std::max<int32_t>(result.interest_bp, 0);
	_economy.interest_rate = static_cast<uint8_t>(std::clamp(charged / 100, 0, 255));
}

static void ApplyIndexStep(uint64_t &index, int32_t annual_bp)
{
	if (annual_bp == 0 || index == 0) return;

	int64_t next = static_cast<int64_t>(index) + static_cast<int64_t>(index) * annual_bp / 120000;
	if (next < (1 << 15)) next = 1 << 15;
	if (next > static_cast<int64_t>(MAX_INFLATION)) next = static_cast<int64_t>(MAX_INFLATION);
	index = static_cast<uint64_t>(next);
}

/** Advance prices, the price index and inflation expectations by one economy month. */
static void AdvancePhillipsCurve(int32_t inflation_bp)
{
	const EconomySettings &cfg = _settings_game.economy;
	const int32_t payment_bp = ClampI32(static_cast<int64_t>(inflation_bp) * cfg.islm_payment_inflation_percent / 100, -5000, 10000);

	ApplyIndexStep(_economy.inflation_prices, inflation_bp);
	ApplyIndexStep(_economy.inflation_payment, payment_bp);

	int64_t price = _economy.macro_price_index;
	price += price * inflation_bp / 120000;
	_economy.macro_price_index = ClampI32(price, MACRO_MIN_PRICE, MACRO_MAX_PRICE);

	const int64_t expected = _economy.macro_expected_inflation_bp;
	const int64_t surprise = static_cast<int64_t>(inflation_bp) - expected;
	_economy.macro_expected_inflation_bp = ClampI32(expected + surprise * cfg.islm_expectation_adaptation_percent / 1200, -5000, 10000);

	const int32_t price_percent = std::clamp(RoundHundredthsToPercent(std::max(inflation_bp, 0)), 0, 255);
	const int32_t payment_percent = std::clamp(price_percent * cfg.islm_payment_inflation_percent / 100, 0, 255);
	_economy.infl_amount = static_cast<uint8_t>(price_percent);
	_economy.infl_amount_pr = static_cast<uint8_t>(payment_percent);
}

static void PublishMacroState()
{
	InvalidateWindowData(WindowClass::MacroEconomy, 0);
	InvalidateWindowClassesData(WindowClass::Finances);
}

static void RecalculateMacroEconomy(bool advance_prices)
{
	if (_economy.macro_price_index <= 0) {
		const int64_t initial = std::max<int32_t>(_settings_game.economy.islm_initial_price_index, 100);
		_economy.macro_price_index = ClampI32(initial * MACRO_PRICE_SCALE, MACRO_MIN_PRICE, MACRO_MAX_PRICE);
		_economy.macro_expected_inflation_bp = _settings_game.economy.islm_initial_expected_inflation * 100;
	}

	const IsLmResult result = SolveIsLm(_economy.macro_price_index);
	const int32_t inflation_bp = result.ok ? PhillipsInflation(_economy.macro_expected_inflation_bp, result.output_gap_bp) : _economy.macro_inflation_bp;
	StoreEquilibrium(result, inflation_bp);
	if (result.ok && advance_prices) AdvancePhillipsCurve(inflation_bp);
	PublishMacroState();
}

void ResetMacroEconomy()
{
	_economy.macro_price_index = 0;
	_economy.macro_output = 0;
	_economy.macro_interest_bp = 0;
	_economy.macro_inflation_bp = 0;
	_economy.macro_expected_inflation_bp = 0;
	_economy.macro_output_gap_bp = 0;
	_economy.macro_consumption = 0;
	_economy.macro_investment = 0;
	_economy.macro_tax_revenue = 0;
	_economy.macro_real_money = 0;
	_economy.macro_ok = 0;

	if (!_settings_game.economy.islm_enabled) return;

	RecalculateMacroEconomy(false);

	/* Prices inherited from 1920 follow the starting expectation. The monthly
	 * Phillips curve takes over once the game is running. */
	const int32_t historical = std::clamp<int32_t>(_settings_game.economy.islm_initial_expected_inflation, 0, 255);
	const int32_t historical_payment = std::clamp(historical * _settings_game.economy.islm_payment_inflation_percent / 100, 0, 255);
	_economy.infl_amount = static_cast<uint8_t>(historical);
	_economy.infl_amount_pr = static_cast<uint8_t>(historical_payment);
}

void RefreshMacroEconomy()
{
	if (_game_mode == GameMode::Menu) return;
	if (!_settings_game.economy.islm_enabled) {
		PublishMacroState();
		return;
	}
	RecalculateMacroEconomy(false);
}

void MacroEconomyMonthly()
{
	if (_game_mode == GameMode::Menu || !_settings_game.economy.islm_enabled) return;

	RecalculateMacroEconomy(true);
	if (_economy.macro_ok == 0) return;

	RecomputePrices();

	if (!_generating_world && TimerGameEconomy::month == 0) {
		AddNewsItem(GetEncodedString(STR_NEWS_MACRO_REPORT,
				RoundHundredthsToPercent(_economy.macro_interest_bp),
				RoundHundredthsToPercent(_economy.macro_inflation_bp),
				RoundHundredthsToPercent(_economy.macro_expected_inflation_bp)),
			NewsType::Economy, NewsStyle::Normal, {});
	}
}

uint MacroActivityPermyriad()
{
	if (_game_mode == GameMode::Menu || !_settings_game.economy.islm_enabled || _economy.macro_ok == 0) return 10000;

	const int64_t link = _settings_game.economy.islm_output_gap_cargo_link;
	const int64_t permyriad = 10000 + static_cast<int64_t>(_economy.macro_output_gap_bp) * link / 100;
	return static_cast<uint>(std::clamp<int64_t>(permyriad, 5000, 20000));
}
