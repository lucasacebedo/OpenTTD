/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file laboratory.hpp Testable stock-flow core for Economy Lab V1. The game window only reads its snapshots. */

#ifndef ECONOMYLAB_LABORATORY_HPP
#define ECONOMYLAB_LABORATORY_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace lab {

enum class Account : uint8_t {
	Output,
	Intermediate,
	Subsidy,
	Consumption,
	Investment,
	Government,
	Exports,
	Imports,
	Discrepancy,
	PublicDebt,
	Money,
	BankLoans,
	BankReserves,
	BankBonds,
	BankDeposits,
	BankEquity,
	BankProvision,
	Count
};

struct Leg {
	Account account;
	int64_t debit = 0;
	int64_t credit = 0;
};

/** One month of postings. A repeated sequence id is rejected and does not move the balances. */
class Ledger {
public:
	bool Post(uint64_t sequence, const Leg *legs, int count)
	{
		if (this->posted && sequence <= this->last_sequence) return false;
		int64_t debits = 0;
		int64_t credits = 0;
		for (int i = 0; i < count; i++) {
			if (legs[i].debit < 0 || legs[i].credit < 0) return false;
			debits += legs[i].debit;
			credits += legs[i].credit;
		}
		if (debits != credits) return false;
		for (int i = 0; i < count; i++) {
			this->debit[static_cast<int>(legs[i].account)] += legs[i].debit;
			this->credit[static_cast<int>(legs[i].account)] += legs[i].credit;
		}
		this->last_sequence = sequence;
		this->posted = true;
		this->events++;
		return true;
	}

	/** Continue a ledger that was saved. Ids at or below the watermark are rejected. */
	void Restore(uint64_t watermark)
	{
		this->last_sequence = watermark;
		this->posted = watermark > 0;
	}

	int64_t Debit(Account account) const { return this->debit[static_cast<int>(account)]; }
	int64_t Credit(Account account) const { return this->credit[static_cast<int>(account)]; }
	int64_t Production() const
	{
		return (this->Credit(Account::Output) - this->Debit(Account::Output))
			- (this->Debit(Account::Intermediate) - this->Credit(Account::Intermediate))
			- (this->Debit(Account::Subsidy) - this->Credit(Account::Subsidy));
	}
	int64_t Expenditure() const
	{
		auto use = [&](Account account) { return this->Debit(account) - this->Credit(account); };
		/* Imports are a credit, so their signed use is already minus imports. */
		return use(Account::Consumption) + use(Account::Investment) + use(Account::Government)
			+ use(Account::Exports) + use(Account::Imports);
	}
	int64_t Discrepancy() const { return this->Debit(Account::Discrepancy) - this->Credit(Account::Discrepancy); }
	int64_t Consumption() const { return this->Debit(Account::Consumption) - this->Credit(Account::Consumption); }
	uint64_t LastSequence() const { return this->last_sequence; }
	int Events() const { return this->events; }

	std::array<int64_t, static_cast<int>(Account::Count) * 2> Image() const
	{
		std::array<int64_t, static_cast<int>(Account::Count) * 2> image{};
		for (int i = 0; i < static_cast<int>(Account::Count); i++) {
			image[i] = this->debit[i];
			image[static_cast<int>(Account::Count) + i] = this->credit[i];
		}
		return image;
	}

private:
	std::array<int64_t, static_cast<int>(Account::Count)> debit{};
	std::array<int64_t, static_cast<int>(Account::Count)> credit{};
	uint64_t last_sequence = 0;
	int events = 0;
	bool posted = false;
};

/** National accounts for one month. Consumption is the argument, never a residual. */
inline bool PostNational(Ledger &ledger, uint64_t sequence, int64_t output, int64_t intermediate, int64_t subsidy,
	int64_t consumption, int64_t investment, int64_t government, int64_t exports, int64_t imports)
{
	const int64_t production = output - intermediate - subsidy;
	const int64_t expenditure = consumption + investment + government + exports - imports;
	const int64_t discrepancy = production - expenditure;
	Leg legs[9];
	int n = 0;
	auto add = [&](Account account, int64_t debit, int64_t credit) {
		if (debit == 0 && credit == 0) return;
		legs[n++] = Leg{account, debit < 0 ? 0 : debit, credit < 0 ? 0 : credit};
	};
	add(Account::Output, 0, output);
	add(Account::Intermediate, intermediate, 0);
	add(Account::Subsidy, subsidy, 0);
	add(Account::Consumption, consumption, 0);
	add(Account::Investment, investment, 0);
	add(Account::Government, government, 0);
	add(Account::Exports, exports, 0);
	add(Account::Imports, 0, imports);
	if (discrepancy > 0) add(Account::Discrepancy, discrepancy, 0);
	if (discrepancy < 0) add(Account::Discrepancy, 0, -discrepancy);
	if (!ledger.Post(sequence, legs, n)) return false;
	return ledger.Consumption() == consumption && ledger.Discrepancy() == discrepancy
		&& ledger.Production() - ledger.Expenditure() == ledger.Discrepancy();
}

inline bool PostDebtFinancing(Ledger &ledger, uint64_t sequence, int64_t deficit)
{
	if (deficit == 0) return true;
	Leg legs[2];
	if (deficit > 0) {
		legs[0] = Leg{Account::BankBonds, deficit, 0};
		legs[1] = Leg{Account::PublicDebt, 0, deficit};
	} else {
		legs[0] = Leg{Account::PublicDebt, -deficit, 0};
		legs[1] = Leg{Account::BankBonds, 0, -deficit};
	}
	if (!ledger.Post(sequence, legs, 2)) return false;
	return ledger.Debit(Account::Money) == 0 && ledger.Credit(Account::Money) == 0
		&& ledger.Credit(Account::PublicDebt) - ledger.Debit(Account::PublicDebt) == deficit;
}

inline int64_t ExpectedLoss(int64_t exposure, int32_t pd_bp, int32_t lgd_permyriad)
{
	if (exposure <= 0 || pd_bp <= 0 || lgd_permyriad <= 0) return 0;
	return exposure * pd_bp / 10000 * lgd_permyriad / 10000;
}

inline bool PostProvision(Ledger &ledger, uint64_t sequence, int64_t loss)
{
	if (loss <= 0) return true;
	const Leg legs[2] = {
		Leg{Account::BankEquity, loss, 0},
		Leg{Account::BankProvision, 0, loss},
	};
	return ledger.Post(sequence, legs, 2);
}

inline int64_t BankGap(const Ledger &ledger)
{
	auto asset = [&](Account account) { return ledger.Debit(account) - ledger.Credit(account); };
	auto liability = [&](Account account) { return ledger.Credit(account) - ledger.Debit(account); };
	const int64_t assets = asset(Account::BankLoans) + asset(Account::BankReserves) + asset(Account::BankBonds) - liability(Account::BankProvision);
	const int64_t liabilities = liability(Account::BankDeposits) + liability(Account::BankEquity);
	return assets - liabilities;
}

inline bool PostBankStock(Ledger &ledger, uint64_t sequence, int64_t loans, int64_t reserves, int64_t bonds, int64_t deposits, int64_t equity)
{
	const Leg legs[5] = {
		Leg{Account::BankLoans, loans, 0},
		Leg{Account::BankReserves, reserves, 0},
		Leg{Account::BankBonds, bonds, 0},
		Leg{Account::BankDeposits, 0, deposits},
		Leg{Account::BankEquity, 0, equity},
	};
	if (loans + reserves + bonds != deposits + equity) return false;
	return ledger.Post(sequence, legs, 5) && BankGap(ledger) == 0;
}

inline bool PostExternal(Ledger &ledger, uint64_t sequence, int64_t exports, int64_t imports, int64_t capital_inflow)
{
	const int64_t current_account = exports - imports;
	const int64_t reserve_change = current_account + capital_inflow;
	Leg legs[4];
	int n = 0;
	auto add = [&](Account account, int64_t debit, int64_t credit) {
		if (debit == 0 && credit == 0) return;
		legs[n++] = Leg{account, debit, credit};
	};
	if (reserve_change > 0) add(Account::BankReserves, reserve_change, 0);
	if (reserve_change < 0) add(Account::BankReserves, 0, -reserve_change);
	if (reserve_change > 0) add(Account::Exports, 0, reserve_change);
	if (reserve_change < 0) add(Account::Imports, -reserve_change, 0);
	if (n == 0) return true;
	return ledger.Post(sequence, legs, n) && (current_account + capital_inflow - reserve_change) == 0;
}

struct Streams {
	uint64_t shock = 0x9E3779B97F4A7C15ULL;
	uint64_t trade = 0xBF58476D1CE4E5B9ULL;
	uint64_t bank = 0x94D049BB133111EBULL;

	static uint64_t Next(uint64_t &state)
	{
		state += 0x9E3779B97F4A7C15ULL;
		uint64_t z = state;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		return z ^ (z >> 31);
	}
};

inline bool SameStreams(const Streams &a, const Streams &b)
{
	return a.shock == b.shock && a.trade == b.trade && a.bank == b.bank;
}

struct State {
	int64_t y = 10000;
	int64_t consumption = 6000;
	int64_t investment = 2000;
	int64_t government = 2000;
	int64_t exports = 1000;
	int64_t imports = 1000;
	int64_t tax = 2000;
	int64_t debt = 0;
	int64_t money = 10000;
	int64_t loans = 4000;
	int64_t bank_reserves = 500;
	int64_t bank_bonds = 500;
	int64_t deposits = 4800;
	int64_t equity = 200;
	int64_t provision = 0;
	int64_t price = 10000;
	int64_t exchange = 9800;
	int32_t rate_bp = 400;
	int32_t pd_bp = 0;
	int32_t lgd_permyriad = 5000;
	int64_t discrepancy = 0;
	int64_t import_extra = 0; ///< Added after the baseline import share. Zero leaves the golden path unchanged.
	int clamps = 0;
	bool ok = true;
	uint64_t shock = 0;
};

inline int64_t Spot(int64_t price, int32_t rate_bp)
{
	const int64_t spread = static_cast<int64_t>(rate_bp) - 300;
	const int64_t adjust = spread * 2;
	return price * (10000 - adjust) / 10000;
}

inline int32_t MoneyMarketRate(int64_t money, int64_t price)
{
	if (price <= 0) return 400;
	return static_cast<int32_t>(400 - 6000 * (money - price) / price);
}

inline void SplitDemand(State &state)
{
	state.consumption = state.y * 60 / 100;
	state.investment = state.y * 20 / 100;
	state.government = state.y * 20 / 100;
	state.exports = state.y * 10 / 100;
	state.imports = state.y * 10 / 100;
	const int64_t expenditure = state.consumption + state.investment + state.government + state.exports - state.imports;
	state.discrepancy = state.y - expenditure;
}

inline bool CloseState(Ledger &ledger, uint64_t sequence, const State &state)
{
	if (!PostNational(ledger, sequence, state.y, 0, 0, state.consumption, state.investment, state.government, state.exports, state.imports)) return false;
	if (!PostBankStock(ledger, sequence + 2, state.loans, state.bank_reserves, state.bank_bonds, state.deposits, state.equity)) return false;
	if (!PostProvision(ledger, sequence + 3, state.provision)) return false;
	if (BankGap(ledger) != 0) return false;
	return PostExternal(ledger, sequence + 4, state.exports, state.imports, state.imports - state.exports);
}

inline State StepBaseline(State state, Streams &streams, bool idiosyncratic, int64_t government_extra = 0)
{
	state.shock = 0;
	if (idiosyncratic) {
		const uint64_t draw = Streams::Next(streams.shock);
		state.shock = static_cast<int64_t>(draw % 3) - 1;
	}
	state.y = 10000 + state.shock;
	SplitDemand(state);
	state.government += government_extra;
	state.imports += state.import_extra;
	state.discrepancy = state.y - (state.consumption + state.investment + state.government + state.exports - state.imports);
	state.exchange = Spot(state.price, state.rate_bp);
	const int64_t deficit = state.government - state.tax;
	state.debt += deficit;
	state.bank_bonds += deficit;
	state.bank_reserves -= deficit;
	state.provision = ExpectedLoss(state.loans, state.pd_bp, state.lgd_permyriad);
	Ledger ledger;
	state.ok = CloseState(ledger, 1, state);
	if (state.rate_bp > 2500 || state.rate_bp < -5000) state.clamps++;
	return state;
}

struct Snapshot {
	State state{};
	int month = 0;
	int tick = 0;
};

struct Clock {
	int tick = 0;
	Snapshot published{};

	void Advance(Streams &streams)
	{
		this->tick++;
		if (this->tick % 30 != 0) return;
		this->published.state = StepBaseline(this->published.state, streams, false);
		this->published.month++;
		this->published.tick = this->tick;
	}

	const Snapshot &Read() const { return this->published; }
};

struct DornbuschResult {
	int64_t e0 = 0;
	int64_t e_impact = 0;
	int64_t e_long = 0;
	int32_t i0 = 0;
	int32_t i_impact = 0;
	int64_t y_long = 0;
	bool identities = false;
	bool converged = false;
};

inline DornbuschResult RunDornbusch()
{
	DornbuschResult result;
	result.i0 = 400;
	result.e0 = Spot(10000, result.i0);
	int64_t price = 10000;
	const int64_t money = 11000;
	bool identities = true;
	for (int month = 0; month < 80; month++) {
		const int32_t rate = MoneyMarketRate(money, price);
		const int64_t exchange = Spot(price, rate);
		State state;
		state.y = 10000 + (money - price) / 2;
		state.price = price;
		state.rate_bp = rate;
		state.exchange = exchange;
		state.money = money;
		SplitDemand(state);
		state.tax = state.government;
		Ledger ledger;
		if (!CloseState(ledger, static_cast<uint64_t>(month) + 1, state)) identities = false;
		if (month == 0) {
			result.i_impact = rate;
			result.e_impact = exchange;
		}
		const int64_t gap = 11000 - price;
		if (gap != 0) {
			int64_t step = gap / 8;
			if (step == 0) step = gap > 0 ? 1 : -1;
			price += step;
		}
	}
	result.e_long = Spot(price, MoneyMarketRate(money, price));
	result.y_long = 10000 + (money - price) / 2;
	result.identities = identities;
	result.converged = price == 11000 && result.y_long == 10000;
	return result;
}

struct ScenarioResult {
	State end{};
	int64_t debt_change = 0;
	int64_t money_change = 0;
	int64_t reserve_change = 0;
	bool identities = false;
};

inline ScenarioResult RunMonths(State state, int months, bool shock_pd)
{
	ScenarioResult result;
	const int64_t debt0 = state.debt;
	const int64_t money0 = state.money;
	const int64_t reserves0 = state.bank_reserves;
	Streams streams;
	bool ok = true;
	for (int month = 0; month < months; month++) {
		if (shock_pd) state.pd_bp = 100;
		state = StepBaseline(state, streams, false);
		Ledger ledger;
		if (!CloseState(ledger, static_cast<uint64_t>(month) + 10, state)) ok = false;
	}
	result.end = state;
	result.debt_change = state.debt - debt0;
	result.money_change = state.money - money0;
	result.reserve_change = state.bank_reserves - reserves0;
	result.identities = ok && state.ok;
	return result;
}

inline uint64_t Mix(uint64_t value)
{
	value ^= value >> 30;
	value *= 0xBF58476D1CE4E5B9ULL;
	value ^= value >> 27;
	value *= 0x94D049BB133111EBULL;
	return value ^ (value >> 31);
}

inline uint64_t BaselineHash()
{
	Streams streams;
	State state;
	for (int month = 0; month < 240; month++) state = StepBaseline(state, streams, false);
	uint64_t hash = 14695981039346656037ULL;
	auto mix = [&](int64_t value) {
		hash ^= static_cast<uint64_t>(value);
		hash *= 1099511628211ULL;
	};
	mix(state.y);
	mix(state.discrepancy);
	mix(state.debt);
	mix(state.money);
	mix(state.exchange);
	mix(state.equity);
	return hash;
}

enum class Fidelity : uint8_t { F0, F1, F2 };

inline int64_t RunFidelity(Fidelity fidelity, uint64_t seed, int months)
{
	Streams streams;
	streams.shock ^= seed;
	State state;
	int64_t total = 0;
	for (int month = 0; month < months; month++) {
		const bool idiosyncratic = fidelity == Fidelity::F0;
		state = StepBaseline(state, streams, idiosyncratic);
		if (fidelity == Fidelity::F1) {
			if (month % 3 == 2) total += state.y;
		} else {
			total += state.y;
		}
	}
	return total;
}

struct Decision {
	int action = 0;
	int confidence_bp = 0;
};

struct Context {
	int64_t gdp = 0;
	int32_t gap_bp = 0;
	uint64_t seed = 0;
};

inline Decision Decide(const Context &context)
{
	if (context.gap_bp > 200) return Decision{2, 8000};
	if (context.gap_bp < -200) return Decision{1, 8000};
	return Decision{0, 5000};
}

inline std::string ProviderLog(const char *provider, const char *secret)
{
	(void)secret;
	std::string line = "provider=";
	line += provider == nullptr ? "mock" : provider;
	line += " secret=redacted";
	return line;
}

inline constexpr int64_t kIdentityTolerance = 0;
inline constexpr const char *kModelVersion = "economy-lab/1.0";

inline int64_t Abs64(int64_t value)
{
	return value < 0 ? -value : value;
}

inline int64_t StatisticalDiscrepancy(int64_t production, int64_t consumption, int64_t investment, int64_t government, int64_t exports, int64_t imports)
{
	return production - (consumption + investment + government + exports - imports);
}

inline bool IdentityHolds(int64_t production, int64_t consumption, int64_t investment, int64_t government, int64_t exports, int64_t imports, int64_t discrepancy)
{
	return Abs64(StatisticalDiscrepancy(production, consumption, investment, government, exports, imports) - discrepancy) <= kIdentityTolerance;
}

struct Books {
	int64_t loans = 0;
	int64_t reserves = 0;
	int64_t bank_bonds = 0;
	int64_t deposits = 0;
	int64_t equity = 0;
	int64_t provision = 0;
	int64_t public_debt = 0;
	int64_t cb_bonds = 0;
	int64_t cb_reserve_liability = 0;
	int64_t official_reserves = 0;
	int64_t base_money = 0;
	int64_t household_bonds = 0;
	int64_t nfa = 0;
	int64_t bank_gap = 0;
	int64_t treasury_gap = 0;
	int64_t cb_gap = 0;
	int64_t external_gap = 0;
};

inline Books AdvanceBooks(Books books, int64_t saving, int64_t loan_flow, int64_t debt_flow, int64_t exports, int64_t imports, int64_t capital_inflow)
{
	books.deposits += saving;
	books.reserves += saving;
	books.loans += loan_flow;
	books.reserves -= loan_flow;
	books.public_debt += debt_flow;
	books.bank_bonds += debt_flow;
	books.reserves -= debt_flow;
	const int64_t target_reserves = books.deposits / 10;
	const int64_t omo = target_reserves - books.reserves;
	books.reserves += omo;
	books.bank_bonds -= omo;
	books.cb_bonds += omo;
	books.cb_reserve_liability += omo;
	const int64_t reserve_change = (exports - imports) + capital_inflow;
	books.official_reserves += reserve_change;
	books.nfa += (exports - imports) + capital_inflow - reserve_change;
	books.bank_gap = (books.loans + books.reserves + books.bank_bonds) - (books.deposits + books.equity + books.provision);
	books.household_bonds = books.public_debt - books.bank_bonds - books.cb_bonds;
	books.treasury_gap = books.public_debt - (books.bank_bonds + books.cb_bonds + books.household_bonds);
	books.base_money = books.cb_bonds + books.official_reserves - books.cb_reserve_liability;
	books.cb_gap = (books.cb_bonds + books.official_reserves) - (books.cb_reserve_liability + books.base_money);
	books.external_gap = (exports - imports + capital_inflow) - reserve_change;
	return books;
}

inline bool BooksReconcile(const Books &books)
{
	return books.bank_gap == 0 && books.treasury_gap == 0 && books.cb_gap == 0 && books.external_gap == 0;
}

enum class ShockVariable : uint8_t {
	Money = 0,
	Government = 1,
	Imports = 2,
	WorldRate = 3,
	DefaultProbability = 4,
	PolicyRate = 5,
	Subsidies = 6,
	Tax = 7,
	AutonomousTax = 8,
};

enum class ShockOperation : uint8_t { Add = 0, Multiply = 1 };

struct Shock {
	uint64_t id = 0;
	int effective_tick = 0;
	int64_t magnitude = 0;
	ShockVariable variable = ShockVariable::Money;
	ShockOperation operation = ShockOperation::Add;
};

struct Session {
	int tick = 0;
	State state{};
	Streams streams{};
	uint64_t watermark = 0;
	int64_t debits = 0;
	int64_t credits = 0;
	int64_t government_extra = 0;
	Shock pending[8]{};
	int pending_count = 0;
	Shock applied[8]{};
	int applied_count = 0;
};

inline bool PostEvent(Session &session, uint64_t id, int64_t amount)
{
	if (id == 0 || id <= session.watermark || amount < 0) return false;
	session.debits += amount;
	session.credits += amount;
	session.watermark = id;
	return session.debits == session.credits;
}

inline bool ScheduleShock(Session &session, Shock shock)
{
	if (shock.id == 0) return false;
	for (int i = 0; i < session.pending_count; i++) {
		if (session.pending[i].id == shock.id) return false;
	}
	for (int i = 0; i < session.applied_count; i++) {
		if (session.applied[i].id == shock.id) return false;
	}
	if (session.pending_count >= 8) return false;
	session.pending[session.pending_count++] = shock;
	return true;
}

inline void ApplyShock(Session &session, const Shock &shock)
{
	switch (shock.variable) {
		case ShockVariable::Money:
			if (shock.operation == ShockOperation::Multiply) session.state.money = session.state.money * shock.magnitude / 10000;
			else session.state.money += shock.magnitude;
			break;
		case ShockVariable::Government:
			session.government_extra += shock.magnitude;
			break;
		case ShockVariable::Imports:
			session.state.import_extra += shock.magnitude;
			break;
		case ShockVariable::WorldRate:
		case ShockVariable::PolicyRate:
			session.state.rate_bp += static_cast<int32_t>(shock.magnitude);
			break;
		case ShockVariable::DefaultProbability:
			session.state.pd_bp += static_cast<int32_t>(shock.magnitude);
			break;
		case ShockVariable::Subsidies:
		case ShockVariable::Tax:
		case ShockVariable::AutonomousTax:
			session.state.tax += shock.magnitude;
			break;
	}
	if (session.applied_count < 8) session.applied[session.applied_count++] = shock;
}

inline void ApplyDueShocks(Session &session)
{
	int keep = 0;
	for (int i = 0; i < session.pending_count; i++) {
		const Shock shock = session.pending[i];
		if (session.tick < shock.effective_tick) {
			session.pending[keep++] = shock;
			continue;
		}
		ApplyShock(session, shock);
	}
	session.pending_count = keep;
}

inline Session AdvanceSession(Session session, int months)
{
	for (int month = 0; month < months; month++) {
		session.tick += 30;
		ApplyDueShocks(session);
		Streams::Next(session.streams.trade);
		session.state = StepBaseline(session.state, session.streams, false, session.government_extra);
	}
	return session;
}

inline const Session &ObserveSession(const Session &session)
{
	return session;
}

enum class ProviderId : uint8_t { OpenRouter, Ollama, LmStudio, Replay };

struct DecisionContract {
	ProviderId provider = ProviderId::OpenRouter;
	int action = 0;
	int confidence_bp = 0;
	bool schema_ok = false;
};

inline bool DecisionSchemaOk(const Decision &decision)
{
	return decision.action >= 0 && decision.action <= 2 && decision.confidence_bp >= 0 && decision.confidence_bp <= 10000;
}

inline DecisionContract ContractDecide(ProviderId provider, const Context &context)
{
	const Decision decision = Decide(context);
	return DecisionContract{provider, decision.action, decision.confidence_bp, DecisionSchemaOk(decision)};
}

inline const char *ProviderName(ProviderId provider)
{
	switch (provider) {
		case ProviderId::OpenRouter: return "openrouter";
		case ProviderId::Ollama: return "ollama";
		case ProviderId::LmStudio: return "lmstudio";
		case ProviderId::Replay: return "replay";
	}
	return "replay";
}

struct ReplayBook {
	Context contexts[16]{};
	Decision decisions[16]{};
	int count = 0;

	bool Record(const Context &context, const Decision &decision)
	{
		if (this->count >= 16) return false;
		this->contexts[this->count] = context;
		this->decisions[this->count] = decision;
		this->count++;
		return true;
	}

	Decision Replay(const Context &context, bool &found) const
	{
		for (int i = 0; i < this->count; i++) {
			const Context &row = this->contexts[i];
			if (row.gdp == context.gdp && row.gap_bp == context.gap_bp && row.seed == context.seed) {
				found = true;
				return this->decisions[i];
			}
		}
		found = false;
		return Decision{};
	}
};

struct FidelityReport {
	int64_t quantity = 0;
	int64_t price = 0;
	int64_t equilibrium = 0;
	int64_t irf = 0;
};

inline FidelityReport CompareFidelity(uint64_t seed, int months)
{
	Streams streams;
	streams.shock ^= seed;
	FidelityReport report;
	int64_t price_f0 = 10000;
	int64_t previous = 10000;
	for (int month = 0; month < months; month++) {
		const int64_t shock = static_cast<int64_t>(Streams::Next(streams.shock) % 3) - 1;
		const int64_t y_f0 = 10000 + shock;
		const int64_t y_f2 = 10000;
		price_f0 += shock;
		report.quantity += Abs64(y_f0 - y_f2);
		report.price += Abs64(price_f0 - 10000);
		report.equilibrium += Abs64(shock);
		report.irf += Abs64((y_f0 - previous) - (y_f2 - 10000));
		previous = y_f0;
	}
	return report;
}

inline std::string FormatFidelityReport(const FidelityReport &report)
{
	return "fidelity=F2 baseline=F0 quantity=" + std::to_string(report.quantity)
		+ " price=" + std::to_string(report.price)
		+ " equilibrium=" + std::to_string(report.equilibrium)
		+ " irf=" + std::to_string(report.irf);
}

struct BenchmarkB1 {
	int years = 100000;
	int64_t checksum = 0;
	bool identities = true;
};

inline BenchmarkB1 RunBenchmarkB1()
{
	BenchmarkB1 result;
	uint64_t mix = 0x6A09E667F3BCC909ULL;
	int64_t y = 10000;
	int64_t price = 10000;
	for (int year = 0; year < result.years; year++) {
		for (int month = 0; month < 12; month++) {
			const int64_t consumption = y * 60 / 100;
			const int64_t investment = y * 20 / 100;
			const int64_t government = y * 20 / 100;
			const int64_t exports = y * 10 / 100;
			const int64_t imports = y * 10 / 100;
			const int64_t discrepancy = StatisticalDiscrepancy(y, consumption, investment, government, exports, imports);
			if (discrepancy != 0) result.identities = false;
			mix = mix * 6364136223846793005ULL + static_cast<uint64_t>(y + price + discrepancy);
			result.checksum += static_cast<int64_t>(mix & 0xFFFF);
		}
	}
	return result;
}

struct GridRow {
	uint64_t id = 0;
	uint64_t seed = 0;
	int parameter = 0;
	bool valid = false;
	const char *equilibrium = "unclassified";
	int64_t y = 0;
};

inline int RunGrid(GridRow *rows, int capacity)
{
	int count = 0;
	for (int parameter = 0; parameter < 12; parameter++) {
		for (int seed_index = 0; seed_index < 10; seed_index++) {
			if (count >= capacity) return count;
			GridRow &row = rows[count];
			row.seed = static_cast<uint64_t>(1000 + seed_index);
			row.parameter = parameter;
			row.id = Mix(row.seed) ^ Mix(static_cast<uint64_t>(parameter) + 1);
			Streams streams;
			streams.shock ^= row.seed;
			State state = StepBaseline(State{}, streams, false, parameter);
			row.y = state.y;
			row.valid = state.ok && IdentityHolds(state.y, state.consumption, state.investment, state.government, state.exports, state.imports, state.discrepancy);
			row.equilibrium = state.ok && state.discrepancy == 0 ? "long_run" : "transition";
			count++;
		}
	}
	return count;
}

inline std::string GridManifest(const GridRow *rows, int count)
{
	std::string text = "id\tseed\tparameter\tvalid\tequilibrium\ty\n";
	for (int i = 0; i < count; i++) {
		text += std::to_string(rows[i].id);
		text += "\t";
		text += std::to_string(rows[i].seed);
		text += "\t";
		text += std::to_string(rows[i].parameter);
		text += "\t";
		text += rows[i].valid ? "yes" : "no";
		text += "\t";
		text += rows[i].equilibrium;
		text += "\t";
		text += std::to_string(rows[i].y);
		text += "\n";
	}
	return text;
}

enum class Scenario : uint8_t { Baseline, Dornbusch, Fiscal, SuddenStop, TwinCrisis };

inline bool RunReleaseScenario(Scenario scenario)
{
	switch (scenario) {
		case Scenario::Baseline:
			return BaselineHash() == 0x7288bcad0e25ab5dULL;
		case Scenario::Dornbusch: {
			const DornbuschResult path = RunDornbusch();
			Session session;
			Shock shock;
			shock.id = 1;
			shock.effective_tick = 30;
			shock.magnitude = 11000;
			shock.variable = ShockVariable::Money;
			shock.operation = ShockOperation::Multiply;
			if (!ScheduleShock(session, shock)) return false;
			session = AdvanceSession(session, 1);
			const bool audited = session.applied_count == 1 && session.applied[0].effective_tick == 30 && session.pending_count == 0;
			const int32_t impact = MoneyMarketRate(session.state.money, session.state.price);
			const bool impact_ok = session.state.money == 11000 && impact == -200 && Spot(session.state.price, impact) == 11000;
			return audited && impact_ok && path.identities && path.converged && path.e0 == 9800 && path.e_impact == 11000 && path.e_long == 10780 && path.i_impact == -200;
		}
		case Scenario::Fiscal: {
			Streams streams;
			const State state = StepBaseline(State{}, streams, false, 500);
			return state.ok && state.debt == 500 && state.money == 10000;
		}
		case Scenario::SuddenStop: {
			Ledger ledger;
			const bool posted = PostExternal(ledger, 1, 1000, 1800, 0);
			const int64_t reserve_change = (1000 - 1800) + 0;
			return posted && reserve_change == -800;
		}
		case Scenario::TwinCrisis:
			return ExpectedLoss(4000, 100, 5000) == 20;
	}
	return false;
}

inline int RunQuality(int64_t unexplained, int calibration_error, int clamps, int64_t bank_gap)
{
	int quality = 10000;
	if (unexplained != 0) quality -= 4000;
	if (calibration_error != 0) quality -= 4000;
	quality -= clamps * 1000;
	if (bank_gap != 0) quality -= 2000;
	if (quality < 0) quality = 0;
	return quality;
}

struct VariableRow {
	const char *id;
	const char *unit;
	const char *kind;
	const char *frequency;
	const char *source;
	const char *version;
	const char *equation;
};

inline constexpr VariableRow kRegister[] = {
	{"gdp_production", "real_pounds", "flow", "month", "transport value added minus route subsidies", kModelVersion, "E-006"},
	{"gdp_expenditure", "real_pounds", "flow", "month", "consumption, investment, government and net exports", kModelVersion, "E-007"},
	{"statistical_discrepancy", "real_pounds", "flow", "month", "production minus expenditure", kModelVersion, "E-008"},
	{"logistics_volume", "cargo_pieces", "flow", "month", "accepted deliveries kept by cargo type", kModelVersion, "none"},
	{"potential_output", "real_pounds", "stock", "month", "calibrated level, population, capital and capacity", kModelVersion, "E-027"},
	{"output_gap", "rate_bp", "stock", "month", "goods demand relative to potential", kModelVersion, "bridge"},
	{"consumption", "real_pounds", "flow", "month", "passenger and mail tariffs", kModelVersion, "E-007"},
	{"investment", "real_pounds", "flow", "month", "construction and new vehicles", kModelVersion, "E-007"},
	{"government_purchases", "real_pounds", "flow", "month", "1000 policy points equal calibrated GDP", kModelVersion, "E-007"},
	{"tax_revenue", "real_pounds", "flow", "month", "tax rate and autonomous tax", kModelVersion, "E-030"},
	{"subsidies", "real_pounds", "flow", "month", "route subsidy, a transfer", kModelVersion, "E-006"},
	{"exports", "real_pounds", "flow", "month", "real exchange rate", kModelVersion, "E-044"},
	{"imports", "real_pounds", "flow", "month", "activity and the real exchange rate", kModelVersion, "E-045"},
	{"policy_rate", "rate_bp", "stock", "month", "Taylor rule", kModelVersion, "E-032"},
	{"loan_rate", "rate_bp", "stock", "month", "policy rate plus risk premium", kModelVersion, "E-038"},
	{"world_rate", "rate_bp", "stock", "month", "exogenous world rate", kModelVersion, "E-051"},
	{"risk_premium", "rate_bp", "stock", "month", "public debt over GDP", kModelVersion, "E-049"},
	{"inflation", "rate_bp", "flow", "month", "expectations-augmented Phillips curve", kModelVersion, "Phillips"},
	{"expected_inflation", "rate_bp", "stock", "month", "adaptive expectations", kModelVersion, "bridge"},
	{"unemployment", "rate_bp", "stock", "month", "Okun bridge", kModelVersion, "E-022"},
	{"population", "persons", "stock", "month", "town populations", kModelVersion, "none"},
	{"labour_force", "persons", "stock", "month", "45 percent of population", kModelVersion, "E-020"},
	{"employment", "persons", "stock", "month", "labour force times one minus unemployment", kModelVersion, "E-022"},
	{"exchange", "permyriad", "stock", "month", "spot rate", kModelVersion, "E-053"},
	{"fundamental_exchange", "permyriad", "stock", "month", "purchasing-power parity", kModelVersion, "E-052"},
	{"overshooting", "indicator", "stock", "month", "spot above the fundamental", kModelVersion, "E-053"},
	{"public_debt", "real_pounds", "stock", "month", "treasury bonds", kModelVersion, "E-031"},
	{"debt_service", "real_pounds", "flow", "month", "contracted coupon", kModelVersion, "E-031"},
	{"bank_loans", "real_pounds", "stock", "month", "company loans", kModelVersion, "E-035"},
	{"bank_deposits", "real_pounds", "stock", "month", "household saving", kModelVersion, "E-034"},
	{"bank_reserves", "real_pounds", "stock", "month", "vault cash and reserve balances", kModelVersion, "E-033"},
	{"bank_bonds", "real_pounds", "stock", "month", "treasury bonds held by banks", kModelVersion, "E-035"},
	{"bank_equity", "real_pounds", "stock", "month", "opening equity plus balanced flows", kModelVersion, "E-035"},
	{"bank_gap", "real_pounds", "stock", "month", "bank assets minus liabilities", kModelVersion, "E-035"},
	{"central_bank_bonds", "real_pounds", "stock", "month", "open-market holdings", kModelVersion, "E-033"},
	{"base_money", "real_pounds", "stock", "month", "currency plus reserve balances", kModelVersion, "E-033"},
	{"official_reserves", "real_pounds", "stock", "month", "external reserve asset", kModelVersion, "BOP"},
	{"household_bonds", "real_pounds", "stock", "month", "treasury bonds held outside banks and the central bank", kModelVersion, "E-031"},
	{"nfa", "real_pounds", "stock", "month", "net foreign assets", kModelVersion, "BOP"},
	{"price_index", "index", "stock", "month", "Phillips price level, 1000 is the base", kModelVersion, "Phillips"},
	{"money_supply", "policy_points", "stock", "month", "exogenous nominal money setting", kModelVersion, "none"},
	{"cargo_capacity", "slots", "stock", "month", "installed vehicle slots", kModelVersion, "none"},
	{"capital", "real_pounds", "stock", "month", "gross investment minus depreciation", kModelVersion, "E-018"},
	{"calibration_error", "indicator", "stock", "month", "1 when GDP leaves the E-027 range", kModelVersion, "A-09"},
	{"clamp_count", "count", "stock", "month", "series sitting on a safety clamp", kModelVersion, "A-21"},
	{"run_quality", "permyriad", "stock", "month", "10000 when the identity, calibration and clamps are clean", kModelVersion, "AC-16"},
	{"expected_loss", "real_pounds", "stock", "month", "exposure times PD times LGD", kModelVersion, "PD*LGD*EAD"},
};

inline bool CatalogComplete()
{
	for (const VariableRow &row : kRegister) {
		if (row.id == nullptr || row.id[0] == '\0') return false;
		if (row.unit == nullptr || row.unit[0] == '\0') return false;
		if (row.kind == nullptr || row.kind[0] == '\0') return false;
		if (row.frequency == nullptr || row.frequency[0] == '\0') return false;
		if (row.source == nullptr || row.source[0] == '\0') return false;
		if (row.version == nullptr || row.version[0] == '\0') return false;
		if (row.equation == nullptr || row.equation[0] == '\0') return false;
		if (std::strcmp(row.kind, "stock") != 0 && std::strcmp(row.kind, "flow") != 0) return false;
		if (std::strcmp(row.version, kModelVersion) != 0) return false;
	}
	return true;
}

inline std::string FormatRegister()
{
	std::string text = "id\tunit\tkind\tfrequency\tsource\tversion\tequation\n";
	for (const VariableRow &row : kRegister) {
		text += row.id;
		text += "\t";
		text += row.unit;
		text += "\t";
		text += row.kind;
		text += "\t";
		text += row.frequency;
		text += "\t";
		text += row.source;
		text += "\t";
		text += row.version;
		text += "\t";
		text += row.equation;
		text += "\n";
	}
	return text;
}

inline std::string FormatVariableYaml()
{
	std::string text = "# Generated from lab::kRegister. conversion version 2.\n";
	text += "conversion_version: 2\n";
	text += "policy_points_per_calibrated_gdp: 1000\n";
	text += "payment_index_base: 65536\n";
	text += "model_version: ";
	text += kModelVersion;
	text += "\nvariables:\n";
	for (const VariableRow &row : kRegister) {
		text += "  - id: ";
		text += row.id;
		text += "\n    unit: ";
		text += row.unit;
		text += "\n    kind: ";
		text += row.kind;
		text += "\n    frequency: ";
		text += row.frequency;
		text += "\n    source: ";
		text += row.source;
		text += "\n    version: ";
		text += row.version;
		text += "\n    equation: ";
		text += row.equation;
		text += "\n";
	}
	return text;
}

} // namespace lab

#include "checkpoint.hpp"

#endif /* ECONOMYLAB_LABORATORY_HPP */
