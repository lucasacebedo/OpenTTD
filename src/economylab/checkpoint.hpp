/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file checkpoint.hpp Save/load image, experiment cards and the thesis configuration texts. Included from laboratory.hpp. */

#ifndef ECONOMYLAB_CHECKPOINT_HPP
#define ECONOMYLAB_CHECKPOINT_HPP

#include "accounts.h"

namespace lab {

inline bool ReadI64(const std::string &text, int64_t &out)
{
	if (text.empty()) return false;
	size_t i = 0;
	bool negative = false;
	if (text[0] == '-') {
		negative = true;
		i = 1;
	}
	if (i >= text.size()) return false;
	uint64_t value = 0;
	for (; i < text.size(); i++) {
		if (text[i] < '0' || text[i] > '9') return false;
		const uint64_t digit = static_cast<uint64_t>(text[i] - '0');
		if (value > (UINT64_MAX - digit) / 10) return false;
		value = value * 10 + digit;
	}
	if (negative) {
		out = -static_cast<int64_t>(value);
	} else {
		out = static_cast<int64_t>(value);
	}
	return true;
}

inline bool ReadU64(const std::string &text, uint64_t &out)
{
	if (text.empty()) return false;
	uint64_t value = 0;
	for (char glyph : text) {
		if (glyph < '0' || glyph > '9') return false;
		const uint64_t digit = static_cast<uint64_t>(glyph - '0');
		if (value > (UINT64_MAX - digit) / 10) return false;
		value = value * 10 + digit;
	}
	out = value;
	return true;
}

inline bool TakeLine(const std::string &text, size_t &pos, std::string &line)
{
	if (pos >= text.size()) return false;
	const size_t end = text.find('\n', pos);
	const size_t stop = end == std::string::npos ? text.size() : end;
	line.assign(text, pos, stop - pos);
	if (!line.empty() && line.back() == '\r') line.pop_back();
	pos = stop < text.size() ? stop + 1 : stop;
	return true;
}

inline bool ReadShockLine(const std::string &line, Shock &shock)
{
	size_t pos = 0;
	auto token = [&](std::string &out) {
		while (pos < line.size() && line[pos] == ' ') pos++;
		if (pos >= line.size()) return false;
		const size_t start = pos;
		if (line[pos] == '-') pos++;
		if (pos >= line.size() || line[pos] < '0' || line[pos] > '9') return false;
		while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') pos++;
		out.assign(line, start, pos - start);
		return true;
	};
	std::string id_text;
	std::string tick_text;
	std::string magnitude_text;
	std::string variable_text;
	std::string operation_text;
	if (!token(id_text) || !token(tick_text) || !token(magnitude_text) || !token(variable_text) || !token(operation_text)) return false;
	uint64_t id = 0;
	int64_t tick = 0;
	int64_t magnitude = 0;
	int64_t variable = 0;
	int64_t operation = 0;
	if (!ReadU64(id_text, id) || !ReadI64(tick_text, tick) || !ReadI64(magnitude_text, magnitude)) return false;
	if (!ReadI64(variable_text, variable) || !ReadI64(operation_text, operation)) return false;
	shock.id = id;
	shock.effective_tick = static_cast<int>(tick);
	shock.magnitude = magnitude;
	shock.variable = static_cast<ShockVariable>(variable);
	shock.operation = static_cast<ShockOperation>(operation);
	return true;
}

inline void AppendShock(std::string &text, const Shock &shock)
{
	text += std::to_string(shock.id);
	text += " ";
	text += std::to_string(shock.effective_tick);
	text += " ";
	text += std::to_string(shock.magnitude);
	text += " ";
	text += std::to_string(static_cast<int>(shock.variable));
	text += " ";
	text += std::to_string(static_cast<int>(shock.operation));
	text += "\n";
}

inline uint64_t SessionHash(const Session &session)
{
	uint64_t hash = 14695981039346656037ULL;
	auto mix = [&](uint64_t value) {
		hash ^= value;
		hash *= 1099511628211ULL;
	};
	mix(static_cast<uint64_t>(session.tick));
	mix(static_cast<uint64_t>(session.state.y));
	mix(static_cast<uint64_t>(session.state.tax));
	mix(static_cast<uint64_t>(session.state.debt));
	mix(static_cast<uint64_t>(session.state.money));
	mix(static_cast<uint64_t>(session.state.price));
	mix(static_cast<uint64_t>(session.state.exchange));
	mix(static_cast<uint64_t>(session.state.rate_bp));
	mix(static_cast<uint64_t>(session.state.pd_bp));
	mix(static_cast<uint64_t>(session.state.lgd_permyriad));
	mix(static_cast<uint64_t>(session.state.loans));
	mix(static_cast<uint64_t>(session.state.bank_reserves));
	mix(static_cast<uint64_t>(session.state.bank_bonds));
	mix(static_cast<uint64_t>(session.state.deposits));
	mix(static_cast<uint64_t>(session.state.equity));
	mix(static_cast<uint64_t>(session.state.provision));
	mix(static_cast<uint64_t>(session.state.import_extra));
	mix(static_cast<uint64_t>(session.state.clamps));
	mix(session.state.ok ? 1 : 0);
	mix(static_cast<uint64_t>(session.government_extra));
	mix(session.streams.shock);
	mix(session.streams.trade);
	mix(session.streams.bank);
	mix(session.watermark);
	mix(static_cast<uint64_t>(session.debits));
	mix(static_cast<uint64_t>(session.credits));
	mix(static_cast<uint64_t>(session.pending_count));
	mix(static_cast<uint64_t>(session.applied_count));
	for (int i = 0; i < session.pending_count && i < 8; i++) {
		mix(session.pending[i].id);
		mix(static_cast<uint64_t>(session.pending[i].effective_tick));
		mix(static_cast<uint64_t>(session.pending[i].magnitude));
		mix(static_cast<uint64_t>(session.pending[i].variable));
		mix(static_cast<uint64_t>(session.pending[i].operation));
	}
	for (int i = 0; i < session.applied_count && i < 8; i++) {
		mix(session.applied[i].id);
		mix(static_cast<uint64_t>(session.applied[i].effective_tick));
		mix(static_cast<uint64_t>(session.applied[i].magnitude));
		mix(static_cast<uint64_t>(session.applied[i].variable));
		mix(static_cast<uint64_t>(session.applied[i].operation));
	}
	return hash;
}

inline std::string FormatCheckpoint(const Session &session)
{
	std::string text = "economylab-checkpoint 1\n";
	auto line = [&](const char *key, int64_t value) {
		text += key;
		text += " ";
		text += std::to_string(value);
		text += "\n";
	};
	auto uline = [&](const char *key, uint64_t value) {
		text += key;
		text += " ";
		text += std::to_string(value);
		text += "\n";
	};
	line("tick", session.tick);
	line("y", session.state.y);
	line("tax", session.state.tax);
	line("debt", session.state.debt);
	line("money", session.state.money);
	line("price", session.state.price);
	line("exchange", session.state.exchange);
	line("rate_bp", session.state.rate_bp);
	line("pd_bp", session.state.pd_bp);
	line("lgd", session.state.lgd_permyriad);
	line("loans", session.state.loans);
	line("reserves", session.state.bank_reserves);
	line("bonds", session.state.bank_bonds);
	line("deposits", session.state.deposits);
	line("equity", session.state.equity);
	line("provision", session.state.provision);
	line("import_extra", session.state.import_extra);
	line("clamps", session.state.clamps);
	line("ok", session.state.ok ? 1 : 0);
	line("government_extra", session.government_extra);
	uline("stream_shock", session.streams.shock);
	uline("stream_trade", session.streams.trade);
	uline("stream_bank", session.streams.bank);
	uline("watermark", session.watermark);
	line("debits", session.debits);
	line("credits", session.credits);
	const int pending = session.pending_count < 0 ? 0 : (session.pending_count > 8 ? 8 : session.pending_count);
	const int applied = session.applied_count < 0 ? 0 : (session.applied_count > 8 ? 8 : session.applied_count);
	line("pending", pending);
	for (int i = 0; i < pending; i++) AppendShock(text, session.pending[i]);
	line("applied", applied);
	for (int i = 0; i < applied; i++) AppendShock(text, session.applied[i]);
	return text;
}

inline bool ParseCheckpoint(const std::string &text, Session &session)
{
	session = Session{};
	size_t pos = 0;
	std::string line;
	if (!TakeLine(text, pos, line) || line != "economylab-checkpoint 1") return false;
	while (TakeLine(text, pos, line)) {
		if (line.empty()) continue;
		const size_t space = line.find(' ');
		if (space == std::string::npos) return false;
		const std::string key = line.substr(0, space);
		const std::string value = line.substr(space + 1);
		if (key == "pending" || key == "applied") {
			int64_t count = 0;
			if (!ReadI64(value, count) || count < 0 || count > 8) return false;
			Shock *dest = key == "pending" ? session.pending : session.applied;
			int &stored = key == "pending" ? session.pending_count : session.applied_count;
			stored = static_cast<int>(count);
			for (int i = 0; i < stored; i++) {
				if (!TakeLine(text, pos, line) || !ReadShockLine(line, dest[i])) return false;
			}
			continue;
		}
		if (key == "stream_shock" || key == "stream_trade" || key == "stream_bank" || key == "watermark") {
			uint64_t number = 0;
			if (!ReadU64(value, number)) return false;
			if (key == "stream_shock") session.streams.shock = number;
			else if (key == "stream_trade") session.streams.trade = number;
			else if (key == "stream_bank") session.streams.bank = number;
			else session.watermark = number;
			continue;
		}
		int64_t number = 0;
		if (!ReadI64(value, number)) return false;
		if (key == "tick") session.tick = static_cast<int>(number);
		else if (key == "y") session.state.y = number;
		else if (key == "tax") session.state.tax = number;
		else if (key == "debt") session.state.debt = number;
		else if (key == "money") session.state.money = number;
		else if (key == "price") session.state.price = number;
		else if (key == "exchange") session.state.exchange = number;
		else if (key == "rate_bp") session.state.rate_bp = static_cast<int32_t>(number);
		else if (key == "pd_bp") session.state.pd_bp = static_cast<int32_t>(number);
		else if (key == "lgd") session.state.lgd_permyriad = static_cast<int32_t>(number);
		else if (key == "loans") session.state.loans = number;
		else if (key == "reserves") session.state.bank_reserves = number;
		else if (key == "bonds") session.state.bank_bonds = number;
		else if (key == "deposits") session.state.deposits = number;
		else if (key == "equity") session.state.equity = number;
		else if (key == "provision") session.state.provision = number;
		else if (key == "import_extra") session.state.import_extra = number;
		else if (key == "clamps") session.state.clamps = static_cast<int>(number);
		else if (key == "ok") session.state.ok = number != 0;
		else if (key == "government_extra") session.government_extra = number;
		else if (key == "debits") session.debits = number;
		else if (key == "credits") session.credits = number;
		else return false;
	}
	return true;
}

/** Twenty years against ten years, a checkpoint image, and ten more. Also checks a passive read and two batch workers. */
inline bool FileResumeMatches(const std::string &saved_text)
{
	uint64_t hashes[240];
	Session continuous;
	for (int month = 0; month < 240; month++) {
		continuous = AdvanceSession(continuous, 1);
		hashes[month] = SessionHash(continuous);
	}
	Session loaded;
	if (!ParseCheckpoint(saved_text, loaded)) return false;
	if (SessionHash(loaded) != hashes[119]) return false;
	for (int month = 120; month < 240; month++) {
		loaded = AdvanceSession(loaded, 1);
		if (SessionHash(loaded) != hashes[month]) return false;
	}
	Session headless = AdvanceSession(Session{}, 6);
	Session shown = headless;
	ObserveSession(shown);
	if (SessionHash(shown) != SessionHash(headless)) return false;
	const Session batch_a = AdvanceSession(Session{}, 24);
	const Session batch_b = AdvanceSession(Session{}, 24);
	return SessionHash(batch_a) == SessionHash(batch_b);
}

inline bool IdentityNow(const State &state)
{
	const int64_t expenditure = state.consumption + state.investment + state.government + state.exports - state.imports;
	return state.ok && state.discrepancy == state.y - expenditure;
}

inline bool ScheduleOne(Session &session, uint64_t id, int tick, int64_t magnitude, ShockVariable variable, ShockOperation operation)
{
	Shock shock;
	shock.id = id;
	shock.effective_tick = tick;
	shock.magnitude = magnitude;
	shock.variable = variable;
	shock.operation = operation;
	return ScheduleShock(session, shock);
}

inline bool RunConfiguredSuite()
{
	{
		const Session session = AdvanceSession(Session{}, 12);
		if (session.applied_count != 0 || session.state.debt != 0 || session.state.money != 10000) return false;
		if (!IdentityNow(session.state)) return false;
	}
	if (!RunReleaseScenario(Scenario::Dornbusch)) return false;
	{
		Session session;
		if (!ScheduleOne(session, 1, 30, 500, ShockVariable::Government, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied_count != 1 || session.applied[0].effective_tick != 30) return false;
		if (session.state.debt != 500 || session.state.money != 10000 || !IdentityNow(session.state)) return false;
	}
	{
		Session hawk;
		Session dove;
		if (!ScheduleOne(hawk, 1, 30, 200, ShockVariable::PolicyRate, ShockOperation::Add)) return false;
		if (!ScheduleOne(dove, 1, 30, -100, ShockVariable::PolicyRate, ShockOperation::Add)) return false;
		hawk = AdvanceSession(hawk, 1);
		dove = AdvanceSession(dove, 1);
		if (hawk.applied[0].effective_tick != 30 || dove.applied[0].effective_tick != 30) return false;
		if (!(hawk.state.exchange < dove.state.exchange)) return false;
	}
	{
		Session session;
		if (!ScheduleOne(session, 1, 30, 100, ShockVariable::WorldRate, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied[0].effective_tick != 30 || session.state.rate_bp != 500 || session.state.exchange != 9600) return false;
	}
	{
		Session session;
		if (!ScheduleOne(session, 1, 30, 800, ShockVariable::Imports, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied[0].effective_tick != 30 || session.state.imports != 1800) return false;
		if (session.state.exports - session.state.imports != -800 || !IdentityNow(session.state)) return false;
		Ledger ledger;
		if (!PostExternal(ledger, 1, 1000, 1800, 0)) return false;
	}
	{
		Session session;
		if (!ScheduleOne(session, 1, 30, 100, ShockVariable::DefaultProbability, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied[0].effective_tick != 30 || session.state.provision != 20) return false;
	}
	{
		Session session;
		if (!ScheduleOne(session, 1, 30, 11000, ShockVariable::Money, ShockOperation::Multiply)) return false;
		if (!ScheduleOne(session, 2, 30, 100, ShockVariable::DefaultProbability, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied_count != 2) return false;
		if (session.applied[0].effective_tick != 30 || session.applied[1].effective_tick != 30) return false;
		if (session.state.money != 11000 || session.state.provision != 20) return false;
		if (MoneyMarketRate(session.state.money, session.state.price) != -200) return false;
	}
	{
		Session session;
		if (!ScheduleOne(session, 1, 30, -200, ShockVariable::Imports, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied[0].effective_tick != 30 || session.state.imports != 800 || !IdentityNow(session.state)) return false;
	}
	{
		Session session;
		if (!ScheduleOne(session, 1, 60, 300, ShockVariable::WorldRate, ShockOperation::Add)) return false;
		session = AdvanceSession(session, 1);
		if (session.applied_count != 0 || session.state.money != 10000 || session.state.rate_bp != 400) return false;
		session = AdvanceSession(session, 1);
		if (session.applied_count != 1 || session.applied[0].effective_tick != 60) return false;
		if (session.state.money != 10000 || session.state.rate_bp != 700) return false;
	}
	{
		const Context context{10000, 0, 101};
		const Decision live = Decide(context);
		ReplayBook book;
		if (!book.Record(context, live)) return false;
		bool found = false;
		const Decision replay = book.Replay(context, found);
		if (!found || replay.action != live.action || replay.confidence_bp != live.confidence_bp) return false;
		const std::string line = ProviderLog("replay", "sk-lab-secret-value");
		if (line.find("sk-lab-secret-value") != std::string::npos) return false;
	}
	return true;
}

inline int ScenarioFileCount()
{
	return 11;
}

inline const char *ScenarioFileName(int index)
{
	switch (index) {
		case 0: return "baseline.yaml";
		case 1: return "dornbusch.yaml";
		case 2: return "fiscal.yaml";
		case 3: return "sudden-stop.yaml";
		case 4: return "twin-crisis.yaml";
		case 5: return "taylor.yaml";
		case 6: return "world-rate.yaml";
		case 7: return "bank-crisis.yaml";
		case 8: return "trade.yaml";
		case 9: return "capital-controls.yaml";
		case 10: return "agents.yaml";
		default: return "baseline.yaml";
	}
}

inline const char *ScenarioFileBody(int index)
{
	switch (index) {
		case 0:
			return
				"experiment:\n"
				"  id: baseline\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 20\n"
				"  warmup_years: 0\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks: []\n"
				"  outputs: [gdp_real, inflation_cpi, policy_rate, fx_nominal, current_account, credit, unemployment]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-00. No shock. Identities and debt stay at the baseline.\n";
		case 1:
			return
				"experiment:\n"
				"  id: dornbusch-money-10\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 40\n"
				"  warmup_years: 10\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: central_bank.base_money_target\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: multiply\n"
				"      value: 1.10\n"
				"  outputs: [gdp_real, inflation_cpi, policy_rate, fx_nominal, fx_real, current_account, credit, unemployment]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-01. Monetary expansion. The spot rate overshoots and the shock stays in the audit.\n";
		case 2:
			return
				"experiment:\n"
				"  id: fiscal-debt\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 20\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: treasury.government_purchases\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: 500\n"
				"  outputs: [gdp_real, public_debt, policy_rate, fx_nominal, current_account]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-02. Higher G is financed with debt. Money does not move.\n";
		case 3:
			return
				"experiment:\n"
				"  id: sudden-stop\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 10\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: external.imports\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: 800\n"
				"  outputs: [gdp_real, fx_nominal, current_account, official_reserves, credit]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-05. Imports jump, the current account is -800 and reserves move with it.\n";
		case 4:
			return
				"experiment:\n"
				"  id: twin-crisis\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 10\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: central_bank.base_money_target\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: multiply\n"
				"      value: 1.10\n"
				"    - variable: banks.default_probability\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: 100\n"
				"  outputs: [gdp_real, fx_nominal, credit, expected_loss, unemployment]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-07. A money jump and a default shock share one effective tick.\n";
		case 5:
			return
				"experiment:\n"
				"  id: taylor-hawk-dove\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 20\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: central_bank.policy_rate\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: 200\n"
				"    - variable: central_bank.policy_rate\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: -100\n"
				"  outputs: [inflation_cpi, policy_rate, fx_nominal, unemployment, credit]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-03. The hawkish rate appreciates the currency against the dovish rate.\n";
		case 6:
			return
				"experiment:\n"
				"  id: world-rate\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 20\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: external.world_rate\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: 100\n"
				"  outputs: [fx_nominal, current_account, official_reserves, gdp_real, credit]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-04. A higher world rate moves the spot rate. Money stays put.\n";
		case 7:
			return
				"experiment:\n"
				"  id: bank-crisis\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 10\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: banks.default_probability\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: 100\n"
				"  outputs: [credit, expected_loss, public_debt, gdp_real]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-06. Probability of default of 100 bp on 4000 of loans gives an expected loss of 20.\n";
		case 8:
			return
				"experiment:\n"
				"  id: trade-liberalisation\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 20\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: external.imports\n"
				"      effective_period: 2030-01-01\n"
				"      effective_tick: 30\n"
				"      operation: add\n"
				"      value: -200\n"
				"  outputs: [gdp_real, current_account, unemployment, fx_nominal]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-08. Lower imports keep the expenditure identity.\n";
		case 9:
			return
				"experiment:\n"
				"  id: capital-controls\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 20\n"
				"  warmup_years: 1\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  shocks:\n"
				"    - variable: external.world_rate\n"
				"      effective_period: 2030-02-01\n"
				"      effective_tick: 60\n"
				"      operation: add\n"
				"      value: 300\n"
				"  outputs: [fx_nominal, official_reserves, gdp_real, policy_rate]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-09. The world-rate shock waits until tick 60. Money is unchanged.\n";
		case 10:
			return
				"experiment:\n"
				"  id: agents-replay\n"
				"  model_version: economy-lab/1.0\n"
				"  fidelity: F2\n"
				"  horizon_years: 5\n"
				"  warmup_years: 0\n"
				"  seeds: [101, 102, 103, 104, 105]\n"
				"  baseline_calibration: emerging_small_open_v1\n"
				"  provider: replay\n"
				"  shocks: []\n"
				"  outputs: [agent_strategy, gdp_real, credit]\n"
				"  stopping:\n"
				"    long_run_equilibrium:\n"
				"      window_months: 60\n"
				"      max_abs_slope: 0.0001\n"
				"      max_market_residual_ppm: 50\n"
				"  hypothesis: EXP-10. A recorded decision replays exactly. The provider log redacts secrets. No network client is used.\n";
		default:
			return "";
	}
}

inline const char *EventsYaml()
{
	return
		"model_version: economy-lab/1.0\n"
		"events:\n"
		"  - id: CargoDelivered\n"
		"    payload: [cargo, units, tariff]\n"
		"    hook: EconomyLabNoteDelivery\n"
		"    postings: [output debit, cash credit]\n"
		"    idempotence: reject a repeated sequence id\n"
		"  - id: SubsidyPaid\n"
		"    payload: [pounds]\n"
		"    hook: EconomyLabNoteSubsidy\n"
		"    postings: [subsidy debit, cash credit]\n"
		"    idempotence: reject a repeated sequence id\n"
		"  - id: MonthClosed\n"
		"    payload: [production, expenditure, exports, imports]\n"
		"    hook: EconomyLabSealIdentity\n"
		"    postings: [national accounts and trade]\n"
		"    idempotence: watermark plus one\n"
		"  - id: PolicyShock\n"
		"    payload: [variable, effective_tick, magnitude]\n"
		"    hook: EconomyLabRecordPolicyShock\n"
		"    postings: [audit trail]\n"
		"    idempotence: a new id for each click\n"
		"  - id: ScheduledShock\n"
		"    payload: [id, effective_tick, variable, operation, magnitude]\n"
		"    hook: ScheduleShock\n"
		"    postings: [applied when the session tick reaches effective_tick]\n"
		"    idempotence: reject a duplicate id\n";
}

inline const char *AccountsYaml()
{
	return
		"model_version: economy-lab/1.0\n"
		"tolerance: 0\n"
		"sectors:\n"
		"  - id: nation\n"
		"    identity: gdp_production = consumption + investment + government + exports - imports + statistical_discrepancy\n"
		"    gap: statistical_discrepancy\n"
		"  - id: banks\n"
		"    identity: loans + reserves + bonds = deposits + equity + provision\n"
		"    gap: bank_gap\n"
		"  - id: treasury\n"
		"    identity: public_debt = bank_bonds + central_bank_bonds + household_bonds\n"
		"    gap: treasury_gap\n"
		"  - id: central_bank\n"
		"    identity: central_bank_bonds + official_reserves = reserve_liability + base_money\n"
		"    gap: cb_gap\n"
		"  - id: external\n"
		"    identity: exports - imports + capital_inflow = reserve_change\n"
		"    gap: external_gap\n";
}

inline const char *CalibrationYaml()
{
	return
		"id: emerging_small_open_v1\n"
		"model_version: economy-lab/1.0\n"
		"date: 2026-11-10\n"
		"seed: 872193\n"
		"distribution: degenerate\n"
		"source: integer baseline of economy-lab/1.0\n"
		"values:\n"
		"  gdp: 10000\n"
		"  money: 10000\n"
		"  price: 10000\n"
		"  policy_rate_bp: 400\n"
		"  exchange: 9800\n"
		"  debt: 0\n"
		"  world_rate_bp: 200\n"
		"  risk_bp: 100\n"
		"  loans: 4000\n"
		"  deposits: 4800\n"
		"  equity: 200\n";
}

inline const char *RegistryYaml()
{
	return
		"model_version: economy-lab/1.0\n"
		"registered_before_results: true\n"
		"seeds: [101, 102, 103, 104, 105]\n"
		"experiments:\n"
		"  - id: EXP-00\n"
		"    file: scenarios/baseline.yaml\n"
		"    treatment: none\n"
		"    outcome: identities and a zero debt path\n"
		"  - id: EXP-01\n"
		"    file: scenarios/dornbusch.yaml\n"
		"    treatment: money times 1.10 at tick 30\n"
		"    outcome: overshooting and an audit row\n"
		"  - id: EXP-02\n"
		"    file: scenarios/fiscal.yaml\n"
		"    treatment: government purchases plus 500\n"
		"    outcome: debt rises by 500 and money stays\n"
		"  - id: EXP-03\n"
		"    file: scenarios/taylor.yaml\n"
		"    treatment: hawkish plus 200 bp against dovish minus 100 bp\n"
		"    outcome: the hawkish spot rate is lower\n"
		"  - id: EXP-04\n"
		"    file: scenarios/world-rate.yaml\n"
		"    treatment: world rate plus 100 bp\n"
		"    outcome: spot rate 9600\n"
		"  - id: EXP-05\n"
		"    file: scenarios/sudden-stop.yaml\n"
		"    treatment: imports plus 800\n"
		"    outcome: current account -800\n"
		"  - id: EXP-06\n"
		"    file: scenarios/bank-crisis.yaml\n"
		"    treatment: default probability plus 100 bp\n"
		"    outcome: expected loss 20\n"
		"  - id: EXP-07\n"
		"    file: scenarios/twin-crisis.yaml\n"
		"    treatment: money times 1.10 and default probability plus 100 bp\n"
		"    outcome: both shocks share tick 30\n"
		"  - id: EXP-08\n"
		"    file: scenarios/trade.yaml\n"
		"    treatment: imports minus 200\n"
		"    outcome: expenditure identity holds\n"
		"  - id: EXP-09\n"
		"    file: scenarios/capital-controls.yaml\n"
		"    treatment: world rate plus 300 bp at tick 60\n"
		"    outcome: tick 30 is unchanged and money stays\n"
		"  - id: EXP-10\n"
		"    file: scenarios/agents.yaml\n"
		"    treatment: replay provider, no network\n"
		"    outcome: the recorded decision matches and the secret stays redacted\n";
}

} // namespace lab

inline void EconomyLabInstallSession(const lab::Session &session)
{
	EconomyLabPersistent &saved = EconomyLabPersistentState();
	saved.lab_tick = session.tick;
	saved.session_y = session.state.y;
	saved.session_tax = session.state.tax;
	saved.session_debt = session.state.debt;
	saved.session_money = session.state.money;
	saved.session_price = session.state.price;
	saved.session_exchange = session.state.exchange;
	saved.session_rate_bp = session.state.rate_bp;
	saved.session_pd_bp = session.state.pd_bp;
	saved.session_lgd = session.state.lgd_permyriad;
	saved.session_loans = session.state.loans;
	saved.session_reserves = session.state.bank_reserves;
	saved.session_bonds = session.state.bank_bonds;
	saved.session_deposits = session.state.deposits;
	saved.session_equity = session.state.equity;
	saved.session_provision = session.state.provision;
	saved.session_clamps = session.state.clamps;
	saved.session_ok = session.state.ok ? 1 : 0;
	saved.government_extra = session.government_extra;
	saved.import_extra = session.state.import_extra;
	saved.stream_shock = session.streams.shock;
	saved.stream_trade = session.streams.trade;
	saved.stream_bank = session.streams.bank;
	saved.session_watermark = session.watermark;
	saved.session_debits = session.debits;
	saved.session_credits = session.credits;
	const int pending = session.pending_count < 0 ? 0 : (session.pending_count > 8 ? 8 : session.pending_count);
	const int applied = session.applied_count < 0 ? 0 : (session.applied_count > 8 ? 8 : session.applied_count);
	saved.pending_count = static_cast<uint8_t>(pending);
	saved.applied_count = static_cast<uint8_t>(applied);
	for (int i = 0; i < 8; i++) {
		const lab::Shock pending_shock = i < pending ? session.pending[i] : lab::Shock{};
		const lab::Shock applied_shock = i < applied ? session.applied[i] : lab::Shock{};
		saved.pending_id[i] = pending_shock.id;
		saved.pending_tick[i] = pending_shock.effective_tick;
		saved.pending_magnitude[i] = pending_shock.magnitude;
		saved.pending_variable[i] = static_cast<uint8_t>(pending_shock.variable);
		saved.pending_operation[i] = static_cast<uint8_t>(pending_shock.operation);
		saved.applied_id[i] = applied_shock.id;
		saved.applied_tick[i] = applied_shock.effective_tick;
		saved.applied_magnitude[i] = applied_shock.magnitude;
		saved.applied_variable[i] = static_cast<uint8_t>(applied_shock.variable);
		saved.applied_operation[i] = static_cast<uint8_t>(applied_shock.operation);
	}
}

inline lab::Session EconomyLabCaptureSession()
{
	const EconomyLabPersistent &saved = EconomyLabPersistentState();
	lab::Session session;
	session.tick = saved.lab_tick;
	session.state.y = saved.session_y;
	session.state.tax = saved.session_tax;
	session.state.debt = saved.session_debt;
	session.state.money = saved.session_money;
	session.state.price = saved.session_price;
	session.state.exchange = saved.session_exchange;
	session.state.rate_bp = saved.session_rate_bp;
	session.state.pd_bp = saved.session_pd_bp;
	session.state.lgd_permyriad = saved.session_lgd;
	session.state.loans = saved.session_loans;
	session.state.bank_reserves = saved.session_reserves;
	session.state.bank_bonds = saved.session_bonds;
	session.state.deposits = saved.session_deposits;
	session.state.equity = saved.session_equity;
	session.state.provision = saved.session_provision;
	session.state.clamps = saved.session_clamps;
	session.state.ok = saved.session_ok != 0;
	session.government_extra = saved.government_extra;
	session.state.import_extra = saved.import_extra;
	session.streams.shock = saved.stream_shock;
	session.streams.trade = saved.stream_trade;
	session.streams.bank = saved.stream_bank;
	session.watermark = saved.session_watermark;
	session.debits = saved.session_debits;
	session.credits = saved.session_credits;
	session.pending_count = saved.pending_count > 8 ? 8 : saved.pending_count;
	session.applied_count = saved.applied_count > 8 ? 8 : saved.applied_count;
	for (int i = 0; i < session.pending_count; i++) {
		session.pending[i].id = saved.pending_id[i];
		session.pending[i].effective_tick = saved.pending_tick[i];
		session.pending[i].magnitude = saved.pending_magnitude[i];
		session.pending[i].variable = static_cast<lab::ShockVariable>(saved.pending_variable[i]);
		session.pending[i].operation = static_cast<lab::ShockOperation>(saved.pending_operation[i]);
	}
	for (int i = 0; i < session.applied_count; i++) {
		session.applied[i].id = saved.applied_id[i];
		session.applied[i].effective_tick = saved.applied_tick[i];
		session.applied[i].magnitude = saved.applied_magnitude[i];
		session.applied[i].variable = static_cast<lab::ShockVariable>(saved.applied_variable[i]);
		session.applied[i].operation = static_cast<lab::ShockOperation>(saved.applied_operation[i]);
	}
	return session;
}

#endif /* ECONOMYLAB_CHECKPOINT_HPP */
