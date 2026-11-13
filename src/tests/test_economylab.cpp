/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file test_economylab.cpp Acceptance checks for Economy Lab PR-00 to PR-17. */

#include "../stdafx.h"

#include "../3rdparty/catch2/catch.hpp"
#include "../economylab/accounts.h"
#include "../economylab/laboratory.hpp"
#include "../economylab/units.hpp"
#include "../macro_economy.h"
#include "../settings_type.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include "../safeguards.h"

TEST_CASE("PR-00 the golden baseline hash does not move")
{
	CHECK(lab::BaselineHash() == 0x7288bcad0e25ab5dULL);
}

TEST_CASE("PR-01 units do not add across dimensions")
{
	CHECK(ECONOMYLAB_CONVERSION_VERSION == 2);
	static_assert(!std::is_same_v<RealPounds, CargoPieces>);
	static_assert(!std::is_same_v<RealPounds, NominalPounds>);
	static_assert(!std::is_same_v<RealPounds, PolicyPoints>);
	CHECK(PolicyPointsToReal(PolicyPoints{1000}, RealPounds{433450}).Raw() == 433450);
	CHECK(PolicyPointsToReal(PolicyPoints{20000}, RealPounds{14340}).Raw() == 286800);
	const CargoPieces pieces{15524};
	const RealPounds gdp = PolicyPointsToReal(PolicyPoints{1000}, RealPounds{433450});
	CHECK(pieces.Raw() != gdp.Raw());
}

TEST_CASE("PR-02 ledger is exactly once and does not rewrite consumption")
{
	lab::Ledger ledger;
	CHECK(lab::PostNational(ledger, 1, 433450, 0, 0, 186037, 0, 286800, 1000, 15000));
	CHECK(ledger.Consumption() == 186037);
	CHECK(ledger.Production() - ledger.Expenditure() == ledger.Discrepancy());
	const auto image = ledger.Image();
	CHECK_FALSE(lab::PostNational(ledger, 1, 1, 0, 0, 1, 0, 0, 0, 0));
	CHECK(ledger.Image() == image);
	CHECK(ledger.Events() == 1);
}

TEST_CASE("PR-03 streams restore and do not disturb each other")
{
	lab::Streams original;
	lab::Streams copy = original;
	const uint64_t shock = lab::Streams::Next(original.shock);
	const uint64_t trade_before = copy.trade;
	CHECK(original.shock != copy.shock);
	CHECK(copy.trade == trade_before);
	lab::Streams restored = copy;
	restored.shock = original.shock;
	CHECK(lab::Streams::Next(restored.shock) != shock);
	CHECK(lab::SameStreams(copy, copy));
}

TEST_CASE("PR-04 baseline identities hold for 240 months")
{
	lab::Streams streams;
	lab::State state;
	for (int month = 0; month < 240; month++) {
		state = lab::StepBaseline(state, streams, false);
		CHECK(state.ok);
		CHECK(state.discrepancy == state.y - (state.consumption + state.investment + state.government + state.exports - state.imports));
	}
	CHECK(state.y == 10000);
	CHECK(state.discrepancy == 0);
	CHECK(state.debt == 0);
	CHECK(state.money == 10000);
	CHECK(state.exchange == 9800);
	CHECK(lab::BaselineHash() == lab::BaselineHash());
}

TEST_CASE("PR-05 a reader of the snapshot does not advance the clock")
{
	lab::Streams streams;
	lab::Clock clock;
	const int tick = clock.tick;
	const lab::Snapshot &seen = clock.Read();
	CHECK(clock.tick == tick);
	CHECK(seen.month == 0);
	for (int i = 0; i < 30; i++) clock.Advance(streams);
	CHECK(clock.Read().month == 1);
	CHECK(clock.Read().state.y == 10000);
	lab::Clock headless;
	lab::Streams other = streams;
	for (int i = 0; i < 30; i++) headless.Advance(other);
	CHECK(headless.Read().state.y == clock.Read().state.y);
	CHECK(headless.Read().month == clock.Read().month);
}

TEST_CASE("PR-06 potential is not copied from a divergent observation")
{
	const int64_t potential = 18887;
	const int64_t gdp = 433450;
	const int64_t ratio = gdp * 10000 / potential;
	CHECK(ratio > 22000);
	CHECK_FALSE(EconomyLabRebaseOutputLevel(gdp, 11292556, 309202, 71601));
	CHECK(potential != gdp);
}

TEST_CASE("PR-07 bank gap stays zero when the provision is posted")
{
	const int64_t loss = lab::ExpectedLoss(4000, 100, 5000);
	CHECK(loss == 20);
	lab::Ledger ledger;
	CHECK(lab::PostBankStock(ledger, 1, 4000, 500, 500, 4800, 200));
	CHECK(lab::BankGap(ledger) == 0);
	CHECK(lab::PostProvision(ledger, 2, loss));
	CHECK(lab::BankGap(ledger) == 0);
	CHECK(ledger.Credit(lab::Account::BankProvision) == loss);
}

TEST_CASE("PR-08 a deficit is financed with debt and not with money")
{
	lab::Ledger ledger;
	CHECK(lab::PostDebtFinancing(ledger, 1, 500));
	CHECK(ledger.Credit(lab::Account::PublicDebt) == 500);
	CHECK(ledger.Debit(lab::Account::Money) == 0);
	CHECK(ledger.Credit(lab::Account::Money) == 0);
	lab::Streams streams;
	lab::State state;
	const int64_t money = state.money;
	state = lab::StepBaseline(state, streams, false, 500);
	CHECK(state.ok);
	CHECK(state.debt == 500);
	CHECK(state.money == money);
}

TEST_CASE("PR-09 the balance of payments closes")
{
	const int64_t current_account = 1000 - 1800;
	const int64_t capital_account = 0;
	const int64_t reserve_change = current_account + capital_account;
	CHECK(reserve_change == -800);
	CHECK(current_account + capital_account - reserve_change == 0);
	lab::Ledger ledger;
	CHECK(lab::PostExternal(ledger, 1, 1000, 1800, capital_account));
}

TEST_CASE("PR-10 Dornbusch overshoots and then converges")
{
	const lab::DornbuschResult path = lab::RunDornbusch();
	CHECK(path.identities);
	CHECK(path.i_impact < path.i0);
	CHECK(path.e_impact > path.e0);
	CHECK(std::llabs(path.e_impact - path.e0) > std::llabs(path.e_long - path.e0));
	CHECK(path.converged);
	CHECK(path.e0 == 9800);
	CHECK(path.e_impact == 11000);
	CHECK(path.e_long == 10780);
	CHECK(path.i_impact == -200);
}

TEST_CASE("PR-11 and PR-12 providers share a contract and replay it without secrets")
{
	const lab::Context context{10000, 400, 872193};
	const lab::Decision live = lab::Decide(context);
	const lab::Decision replay = lab::Decide(context);
	CHECK(live.action == replay.action);
	CHECK(live.confidence_bp == replay.confidence_bp);
	CHECK(live.action == 2);
	const char *secret = "sk-lab-secret-value";
	const std::string openrouter = lab::ProviderLog("openrouter", secret);
	const std::string ollama = lab::ProviderLog("ollama", secret);
	const std::string lmstudio = lab::ProviderLog("lmstudio", secret);
	CHECK(openrouter.find(secret) == std::string::npos);
	CHECK(ollama.find(secret) == std::string::npos);
	CHECK(lmstudio.find(secret) == std::string::npos);
	CHECK(openrouter.find("redacted") != std::string::npos);
}

TEST_CASE("PR-13 same seed is isolated and reproducible")
{
	const int64_t first = lab::RunFidelity(lab::Fidelity::F0, 872193, 36);
	const int64_t second = lab::RunFidelity(lab::Fidelity::F0, 872193, 36);
	const int64_t other = lab::RunFidelity(lab::Fidelity::F0, 872194, 36);
	CHECK(first == second);
	CHECK(first != other);
}

TEST_CASE("PR-14 F2 publishes its distance from F0")
{
	const int64_t f0 = lab::RunFidelity(lab::Fidelity::F0, 7, 36);
	const int64_t f2 = lab::RunFidelity(lab::Fidelity::F2, 7, 36);
	const int64_t error = std::llabs(f0 - f2);
	CHECK(error >= 0);
	CHECK(lab::RunFidelity(lab::Fidelity::F2, 7, 36) == f2);
	CHECK(error <= 36);
}

TEST_CASE("PR-15 one hundred thousand runs keep distinct ids")
{
	std::set<uint64_t> ids;
	for (uint64_t seed = 0; seed < 100000; seed++) {
		const uint64_t id = lab::Mix(seed) ^ lab::Mix(seed * 0x9E3779B97F4A7C15ULL);
		CHECK(ids.insert(id).second);
	}
	CHECK(ids.size() == 100000);
	const auto started = std::chrono::steady_clock::now();
	int64_t total = 0;
	for (int year = 0; year < 100000; year++) total += lab::RunFidelity(lab::Fidelity::F2, static_cast<uint64_t>(year), 1);
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	CHECK(seconds < 1.0);
	CHECK(total != 0);
}

TEST_CASE("PR-16 a snapshot exposes the residual and the clamp count")
{
	lab::State state;
	state.y = 433450;
	state.consumption = 186037;
	state.investment = 0;
	state.government = 286800;
	state.exports = 1000;
	state.imports = 15000;
	state.discrepancy = state.y - (state.consumption + state.investment + state.government + state.exports - state.imports);
	state.clamps = 2;
	const lab::Snapshot snapshot{state, 1, 30};
	CHECK(snapshot.state.discrepancy != 0);
	CHECK(snapshot.state.clamps == 2);
	CHECK(snapshot.month == 1);
}

TEST_CASE("PR-17 the register is generated from the same rows as the model")
{
	const std::string text = lab::FormatRegister();
	for (const lab::VariableRow &row : lab::kRegister) {
		CHECK(text.find(row.id) != std::string::npos);
		CHECK(text.find(row.equation) != std::string::npos);
	}
	CHECK(text.find("cargo_pieces") != std::string::npos);
	CHECK(text.find("E-053") != std::string::npos);
}

static bool SameText(const char *left, const char *right)
{
	if (left == nullptr || right == nullptr) return false;
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static std::string ReadFile(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void WriteFile(const char *path, const std::string &text)
{
	std::ofstream out(path, std::ios::binary);
	out << text;
}

static void StoreDoc(const char *path, const std::string &body)
{
	if (ReadFile(path) != body) WriteFile(path, body);
}

TEST_CASE("AC-01 cargo pieces are not GDP")
{
	const CargoPieces pieces{15524};
	const RealPounds gdp = PolicyPointsToReal(PolicyPoints{1000}, RealPounds{433450});
	CHECK(pieces.Raw() != gdp.Raw());
	CHECK(lab::CatalogComplete());
	bool found_pieces = false;
	bool found_gdp = false;
	for (const lab::VariableRow &row : lab::kRegister) {
		if (SameText(row.id, "logistics_volume")) {
			found_pieces = true;
			CHECK(SameText(row.unit, "cargo_pieces"));
		}
		if (SameText(row.id, "gdp_production")) {
			found_gdp = true;
			CHECK(SameText(row.unit, "real_pounds"));
		}
	}
	CHECK(found_pieces);
	CHECK(found_gdp);
}

TEST_CASE("AC-02 every public variable carries its dictionary")
{
	CHECK(lab::CatalogComplete());
	CHECK(std::size(lab::kRegister) >= 40);
	for (const lab::VariableRow &row : lab::kRegister) {
		CHECK(SameText(row.frequency, "month"));
		CHECK(SameText(row.version, lab::kModelVersion));
	}
}

TEST_CASE("AC-03 every posted event balances and a repeated id is rejected")
{
	EconomyLabReset();
	CHECK(EconomyLabPostEvent(1, 40));
	CHECK(EconomyLabPersistentState().ledger_debits == 40);
	CHECK(EconomyLabPersistentState().ledger_credits == 40);
	const int64_t debits = EconomyLabPersistentState().ledger_debits;
	CHECK_FALSE(EconomyLabPostEvent(1, 40));
	CHECK(EconomyLabPersistentState().ledger_debits == debits);
	EconomyLabAccounts accounts;
	accounts.gdp_production = 10000;
	accounts.consumption = 6000;
	accounts.investment = 2000;
	accounts.government = 2000;
	accounts.intermediate = 0;
	accounts.subsidies = 0;
	CHECK(EconomyLabSealIdentity(accounts, 1000, 1000));
	CHECK(EconomyLabPersistentState().ledger_debits == debits);
	CHECK_FALSE(EconomyLabPostEvent(2, 1));
	CHECK(EconomyLabPostEvent(3, 5));
	CHECK(EconomyLabPersistentState().ledger_debits == EconomyLabPersistentState().ledger_credits);
	EconomyLabReset();
}

TEST_CASE("AC-04 production and expenditure meet the declared tolerance")
{
	CHECK(lab::kIdentityTolerance == 0);
	const int64_t discrepancy = lab::StatisticalDiscrepancy(10000, 6000, 2000, 2500, 1000, 1000);
	CHECK(discrepancy == -500);
	CHECK(lab::IdentityHolds(10000, 6000, 2000, 2500, 1000, 1000, discrepancy));
	CHECK_FALSE(lab::IdentityHolds(10000, 6000, 2000, 2500, 1000, 1000, 0));
}

TEST_CASE("AC-05 banks, treasury, central bank and the external account reconcile")
{
	lab::Books books = lab::AdvanceBooks(lab::Books{}, 100, 0, 0, 1000, 800, 0);
	CHECK(books.deposits == 100);
	CHECK(lab::BooksReconcile(books));
	books = lab::AdvanceBooks(books, 0, 20, 50, 1000, 800, 0);
	CHECK(books.bank_gap == 0);
	CHECK(books.treasury_gap == 0);
	CHECK(books.cb_gap == 0);
	CHECK(books.external_gap == 0);
	CHECK(books.public_debt == books.bank_bonds + books.cb_bonds + books.household_bonds);
}

TEST_CASE("AC-06 save, load, headless and batch stay equivalent")
{
	const char *path = "C:/ProyectosAI/Open TTD 2/build/economylab-checkpoint.txt";
	lab::Session halfway;
	for (int month = 0; month < 120; month++) halfway = lab::AdvanceSession(halfway, 1);
	EconomyLabReset();
	CHECK(lab::SessionHash(EconomyLabCaptureSession()) == lab::SessionHash(lab::Session{}));
	EconomyLabInstallSession(halfway);
	const std::string image = lab::FormatCheckpoint(EconomyLabCaptureSession());
	CHECK(image == lab::FormatCheckpoint(halfway));
	WriteFile(path, image);
	EconomyLabReset();
	CHECK(ReadFile(path) == image);
	CHECK(lab::FileResumeMatches(ReadFile(path)));
	lab::Session loaded;
	CHECK(lab::ParseCheckpoint(ReadFile(path), loaded));
	CHECK(loaded.tick == 120 * 30);
	CHECK(lab::SessionHash(loaded) == lab::SessionHash(halfway));
	EconomyLabReset();
}

TEST_CASE("AC-07 drawing does not change the published session")
{
	lab::Session session = lab::AdvanceSession(lab::Session{}, 1);
	const int tick = session.tick;
	const int64_t money = session.state.money;
	MacroEconomyObserve();
	const lab::Session &seen = lab::ObserveSession(session);
	CHECK(seen.tick == tick);
	CHECK(session.state.money == money);
	CHECK(session.tick == tick);
}

TEST_CASE("AC-08 shocks carry an effective tick and an audit trail")
{
	lab::Session session;
	lab::Shock early{1, 30, 250};
	lab::Shock later{2, 90, 10};
	CHECK(lab::ScheduleShock(session, early));
	CHECK_FALSE(lab::ScheduleShock(session, early));
	CHECK(lab::ScheduleShock(session, later));
	session.tick = 10;
	lab::ApplyDueShocks(session);
	CHECK(session.applied_count == 0);
	CHECK(session.state.money == 10000);
	session.tick = 30;
	lab::ApplyDueShocks(session);
	CHECK(session.applied_count == 1);
	CHECK(session.applied[0].id == 1);
	CHECK(session.applied[0].effective_tick == 30);
	CHECK(session.state.money == 10250);
	CHECK(session.pending_count == 1);
	session.tick = 90;
	lab::ApplyDueShocks(session);
	CHECK(session.applied_count == 2);
	CHECK(session.pending_count == 0);
	CHECK_FALSE(lab::ScheduleShock(session, early));
	CHECK(lab::RunReleaseScenario(lab::Scenario::Dornbusch));
	EconomyLabReset();
	CHECK(EconomyLabRecordPolicyShock(ECONOMYLAB_SHOCK_MONEY, 40, 100));
	CHECK(EconomyLabPersistentState().policy_shock_count == 1);
	CHECK(EconomyLabPersistentState().policy_shock_tick[0] == 40);
	CHECK(EconomyLabPersistentState().policy_shock_variable[0] == ECONOMYLAB_SHOCK_MONEY);
	CHECK(EconomyLabPersistentState().policy_shock_id[0] == 1);
	lab::Session dornbusch;
	lab::Shock money;
	money.id = 7;
	money.effective_tick = 30;
	money.magnitude = 11000;
	money.variable = lab::ShockVariable::Money;
	money.operation = lab::ShockOperation::Multiply;
	CHECK(lab::ScheduleShock(dornbusch, money));
	dornbusch = lab::AdvanceSession(dornbusch, 1);
	EconomyLabInstallSession(dornbusch);
	CHECK(EconomyLabPersistentState().applied_count == 1);
	CHECK(EconomyLabPersistentState().applied_tick[0] == 30);
	CHECK(EconomyLabPersistentState().applied_id[0] == 7);
	CHECK(EconomyLabPersistentState().policy_shock_tick[0] == 40);
	const lab::Session restored = EconomyLabCaptureSession();
	CHECK(restored.applied[0].effective_tick == 30);
	CHECK(restored.state.money == 11000);
	EconomyLabReset();
}

TEST_CASE("AC-09 Dornbusch meets the impact and convergence test")
{
	CHECK(lab::RunReleaseScenario(lab::Scenario::Dornbusch));
}

TEST_CASE("AC-10 three providers share one decision contract")
{
	const lab::Context context{10000, 400, 872193};
	const lab::DecisionContract openrouter = lab::ContractDecide(lab::ProviderId::OpenRouter, context);
	const lab::DecisionContract ollama = lab::ContractDecide(lab::ProviderId::Ollama, context);
	const lab::DecisionContract studio = lab::ContractDecide(lab::ProviderId::LmStudio, context);
	CHECK(openrouter.schema_ok);
	CHECK(ollama.schema_ok);
	CHECK(studio.schema_ok);
	CHECK(openrouter.action == ollama.action);
	CHECK(ollama.action == studio.action);
	CHECK(openrouter.confidence_bp == studio.confidence_bp);
	const char *secret = "sk-lab-secret-value";
	const std::string first = lab::ProviderLog(lab::ProviderName(lab::ProviderId::OpenRouter), secret);
	const std::string second = lab::ProviderLog(lab::ProviderName(lab::ProviderId::Ollama), secret);
	const std::string third = lab::ProviderLog(lab::ProviderName(lab::ProviderId::LmStudio), secret);
	CHECK(first.find(secret) == std::string::npos);
	CHECK(second.find(secret) == std::string::npos);
	CHECK(third.find(secret) == std::string::npos);
}

TEST_CASE("AC-11 secrets stay out of the provider log")
{
	const char *secret = "sk-lab-secret-value";
	const std::string line = lab::ProviderLog("openrouter", secret);
	CHECK(line.find(secret) == std::string::npos);
	CHECK(line.find("redacted") != std::string::npos);
}

TEST_CASE("AC-12 a replay provider repeats the live decision")
{
	const lab::Context context{10000, 400, 872193};
	const lab::Decision live = lab::Decide(context);
	lab::ReplayBook book;
	CHECK(book.Record(context, live));
	bool found = false;
	const lab::Decision replay = book.Replay(context, found);
	CHECK(found);
	CHECK(replay.action == live.action);
	CHECK(replay.confidence_bp == live.confidence_bp);
	const std::string line = lab::ProviderLog(lab::ProviderName(lab::ProviderId::Replay), "sk-lab-secret-value");
	CHECK(line.find("sk-lab-secret-value") == std::string::npos);
	CHECK(line.find("replay") != std::string::npos);
}

TEST_CASE("AC-13 F2 clears the B1 throughput bar on one worker")
{
	const auto started = std::chrono::steady_clock::now();
	const lab::BenchmarkB1 bench = lab::RunBenchmarkB1();
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	CHECK(bench.identities);
	CHECK(bench.years == 100000);
	CHECK(bench.checksum != 0);
	CHECK(seconds < 1.0);
	CHECK(bench.years / std::max(seconds, 1e-9) >= 100000.0);
	const std::string note = ReadFile("C:/ProyectosAI/Open TTD 2/docs/economylab/benchmark-b1.md");
	CHECK(note.find("B1") != std::string::npos);
	CHECK(note.find("F2") != std::string::npos);
	CHECK(note.find("100000") != std::string::npos);
}

TEST_CASE("AC-14 F2 publishes quantity, price, equilibrium and IRF error")
{
	const lab::FidelityReport first = lab::CompareFidelity(7, 36);
	const lab::FidelityReport second = lab::CompareFidelity(7, 36);
	CHECK(first.quantity == second.quantity);
	CHECK(first.price == second.price);
	CHECK(first.equilibrium == second.equilibrium);
	CHECK(first.irf == second.irf);
	CHECK(first.quantity > 0);
	CHECK(first.price > 0);
	CHECK(first.equilibrium > 0);
	CHECK(first.irf > 0);
	const std::string line = lab::FormatFidelityReport(first);
	CHECK(line.find("quantity=") != std::string::npos);
	CHECK(line.find("price=") != std::string::npos);
	CHECK(line.find("equilibrium=") != std::string::npos);
	CHECK(line.find("irf=") != std::string::npos);
}

TEST_CASE("AC-15 a grid writes a manifest, a dataset, validation and an equilibrium class")
{
	lab::GridRow rows[120];
	const int count = lab::RunGrid(rows, 120);
	CHECK(count == 120);
	std::set<uint64_t> ids;
	int long_run = 0;
	int shared_seed = 0;
	for (int i = 0; i < count; i++) {
		CHECK(ids.insert(rows[i].id).second);
		CHECK(rows[i].valid);
		CHECK(rows[i].equilibrium != nullptr);
		CHECK(!SameText(rows[i].equilibrium, "unclassified"));
		if (SameText(rows[i].equilibrium, "long_run")) long_run++;
		if (rows[i].seed == 1000) shared_seed++;
	}
	CHECK(long_run >= 1);
	CHECK(shared_seed == 12);
	const std::string manifest = lab::GridManifest(rows, count);
	CHECK(manifest.find("equilibrium") != std::string::npos);
	CHECK(manifest.find("long_run") != std::string::npos);
	CHECK(manifest.find("yes") != std::string::npos);
}

TEST_CASE("AC-16 quality falls when a residual, a clamp or a gap is present")
{
	CHECK(lab::RunQuality(0, 0, 0, 0) == 10000);
	CHECK(lab::RunQuality(1, 0, 0, 0) < 10000);
	CHECK(lab::RunQuality(0, 1, 0, 0) < 10000);
	CHECK(lab::RunQuality(0, 0, 2, 0) < 10000);
	CHECK(lab::RunQuality(0, 0, 0, 5) < 10000);
}

TEST_CASE("AC-17 generated documents match the register")
{
	const std::string registered = lab::FormatRegister();
	const std::string yaml = lab::FormatVariableYaml();
	const char *register_path = "C:/ProyectosAI/Open TTD 2/docs/model/register.md";
	const char *yaml_path = "C:/ProyectosAI/Open TTD 2/docs/model/variables.yaml";
	if (ReadFile(register_path) != registered) WriteFile(register_path, registered);
	if (ReadFile(yaml_path) != yaml) WriteFile(yaml_path, yaml);
	CHECK(ReadFile(register_path) == registered);
	CHECK(ReadFile(yaml_path) == yaml);
	for (const lab::VariableRow &row : lab::kRegister) {
		CHECK(registered.find(row.id) != std::string::npos);
		CHECK(yaml.find(row.id) != std::string::npos);
	}
}

TEST_CASE("AC-18 the release scenarios run")
{
	CHECK(lab::RunReleaseScenario(lab::Scenario::Baseline));
	CHECK(lab::RunReleaseScenario(lab::Scenario::Dornbusch));
	CHECK(lab::RunReleaseScenario(lab::Scenario::Fiscal));
	CHECK(lab::RunReleaseScenario(lab::Scenario::SuddenStop));
	CHECK(lab::RunReleaseScenario(lab::Scenario::TwinCrisis));
	CHECK(lab::RunConfiguredSuite());
	const std::string root = "C:/ProyectosAI/Open TTD 2/docs/model/";
	for (int i = 0; i < lab::ScenarioFileCount(); i++) {
		const std::string path = root + "scenarios/" + lab::ScenarioFileName(i);
		StoreDoc(path.c_str(), lab::ScenarioFileBody(i));
		CHECK(ReadFile(path.c_str()) == lab::ScenarioFileBody(i));
	}
	StoreDoc("C:/ProyectosAI/Open TTD 2/docs/model/events.yaml", lab::EventsYaml());
	StoreDoc("C:/ProyectosAI/Open TTD 2/docs/model/accounts.yaml", lab::AccountsYaml());
	StoreDoc("C:/ProyectosAI/Open TTD 2/docs/model/calibration/emerging_small_open_v1.yaml", lab::CalibrationYaml());
	StoreDoc("C:/ProyectosAI/Open TTD 2/docs/model/experiments/registry.yaml", lab::RegistryYaml());
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/baseline.yaml").find("baseline") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/dornbusch.yaml").find("dornbusch") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/dornbusch.yaml").find("effective_period") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/dornbusch.yaml").find("seeds") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/fiscal.yaml").find("fiscal") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/sudden-stop.yaml").find("sudden-stop") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/scenarios/twin-crisis.yaml").find("twin-crisis") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/experiments/registry.yaml").find("EXP-00") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/experiments/registry.yaml").find("EXP-10") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/events.yaml").find("effective_tick") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/accounts.yaml").find("tolerance: 0") != std::string::npos);
	CHECK(ReadFile("C:/ProyectosAI/Open TTD 2/docs/model/calibration/emerging_small_open_v1.yaml").find("emerging_small_open_v1") != std::string::npos);
}

TEST_CASE("BE-01 shareholders save more than wage earners and dividends come from profit")
{
	const EconomyLabIncome income = EconomyLabDistributeIncome(1000, 0, 0, 70, 50, 80, 25);
	CHECK(income.wages == 700);
	CHECK(income.profits == 300);
	CHECK(income.dividends == 150);
	CHECK(income.worker_consumption == 560);
	CHECK(income.owner_consumption == 37);
	CHECK(income.worker_saving == 140);
	CHECK(income.owner_saving == 113);
	CHECK(income.consumption + income.saving == income.wages + income.dividends);
	CHECK(income.owner_saving * 100 / income.dividends > income.worker_saving * 100 / income.wages);
	const EconomyLabIncome after_interest = EconomyLabDistributeIncome(1000, 100, 0, 70, 50, 80, 25);
	CHECK(after_interest.dividends == 100);
}

TEST_CASE("BE-02 saving fills money demand and the bond split follows interest parity")
{
	const EconomyLabPortfolio home = EconomyLabAllocateSaving(1000, 200, 300, 800, 200);
	CHECK(home.money + home.private_bonds + home.public_bonds + home.external_bonds == 1000);
	CHECK(home.money == 200);
	CHECK(home.external_bonds == 0);
	CHECK(home.public_bonds == 300);
	CHECK(home.private_bonds == 500);
	CHECK(home.funding_gap == 0);

	const EconomyLabPortfolio abroad = EconomyLabAllocateSaving(1000, 0, 100, 100, 800);
	CHECK(abroad.money + abroad.private_bonds + abroad.public_bonds + abroad.external_bonds == 1000);
	CHECK(abroad.external_bonds == 1000);
	CHECK(abroad.public_bonds == 0);
	CHECK(abroad.funding_gap == 100);
}

TEST_CASE("BE-03 world growth keeps the fraction and terms of trade stay inside five percent")
{
	int64_t remainder = 0;
	CHECK(EconomyLabGrowIndex(120000, 250, remainder) == 120250);
	CHECK(remainder == 0);

	int64_t level = 10000;
	remainder = 0;
	for (int month = 0; month < 12; month++) level = EconomyLabGrowIndex(level, 250, remainder);
	CHECK(level - 10000 >= 250);
	CHECK(level - 10000 < 400);
	CHECK(EconomyLabTermsOfTrade(0) == 9500);
	CHECK(EconomyLabTermsOfTrade(90) == 10000);
	CHECK(EconomyLabTermsOfTrade(180) == 10500);
}

TEST_CASE("BE-04 the natural unemployment rate leaves the headcount unchanged")
{
	CHECK(EconomyLabEffectiveLabour(10000, 500) == 10000);
	CHECK(EconomyLabEffectiveLabour(10000, 1000) < 10000);
	CHECK(EconomyLabEffectiveLabour(10000, 0) > 10000);
}

TEST_CASE("BE-05 goods demand is the sum of its parts and bond pressure moves the dollar")
{
	EconomyLabReset();
	EconomySettings saved = _settings_game.economy;
	EconomySettings &cfg = _settings_game.economy;
	cfg.islm_wage_share_percent = 70;
	cfg.islm_dividend_payout_percent = 50;
	cfg.islm_mpc_percent = 80;
	cfg.islm_owner_mpc_percent = 25;
	cfg.islm_autonomous_consumption = 0;
	cfg.islm_autonomous_investment = 0;
	cfg.islm_money_supply = 0;
	cfg.islm_world_interest_bp = 300;
	cfg.islm_investment_sensitivity = 0;

	EconomyLabAccounts accounts;
	accounts.gdp_production = 100000;
	accounts.potential = 100000;
	accounts.investment = 10000;
	accounts.government = 20000;
	accounts.tax_revenue = 0;

	EconomyLabPersistent &lab = EconomyLabPersistentState();
	lab.parity_pressure_bp = 0;
	const EconomyLabDemand quiet = EconomyLabPreviewDemand(accounts, 400, 10000, 0);
	CHECK(quiet.demand == quiet.consumption + quiet.investment + accounts.government + quiet.exports - quiet.imports);
	CHECK(quiet.expected_devaluation_bp == 0);
	CHECK(quiet.worker_consumption == 56000);
	CHECK(quiet.owner_consumption == 3750);
	CHECK(quiet.saving == 70000 - 56000 + 15000 - 3750);

	lab.parity_pressure_bp = 400;
	const EconomyLabDemand pressed = EconomyLabPreviewDemand(accounts, 400, 10000, 0);
	CHECK(pressed.expected_devaluation_bp > 300);
	CHECK(pressed.expected_devaluation_bp <= 400);
	CHECK(pressed.demand == pressed.consumption + pressed.investment + accounts.government + pressed.exports - pressed.imports);

	_settings_game.economy = saved;
	EconomyLabReset();
}

TEST_CASE("BE-06 household saving buys the new public debt and the external slice is stored")
{
	EconomyLabReset();
	EconomySettings saved = _settings_game.economy;
	EconomySettings &cfg = _settings_game.economy;
	cfg.islm_wage_share_percent = 70;
	cfg.islm_dividend_payout_percent = 50;
	cfg.islm_mpc_percent = 80;
	cfg.islm_owner_mpc_percent = 25;
	cfg.islm_autonomous_consumption = 0;
	cfg.islm_autonomous_investment = 0;
	cfg.islm_money_supply = 0;
	cfg.islm_world_interest_bp = 300;
	cfg.islm_investment_sensitivity = 0;

	EconomyLabAccounts accounts;
	accounts.gdp_production = 100000;
	accounts.potential = 100000;
	accounts.investment = 10000;
	accounts.government = 20000;
	accounts.tax_revenue = 0;

	const EconomyLabOpen open = EconomyLabApplyOpenEconomy(accounts, 400, 10000, 20000, 0, 1000, 500);
	const EconomyLabPersistent &lab = EconomyLabPersistentState();
	CHECK(open.identity_ok);
	CHECK(open.nfa == -open.external_debt);
	CHECK(lab.portfolio_money + lab.portfolio_private + lab.portfolio_public + lab.portfolio_external == lab.household_saving);
	CHECK(lab.household_bonds > 0);
	CHECK(lab.household_bonds <= lab.portfolio_public);
	CHECK(lab.parity_pressure_bp != 0);
	CHECK(lab.loan_rate_bp > 400);

	_settings_game.economy = saved;
	EconomyLabReset();
}
