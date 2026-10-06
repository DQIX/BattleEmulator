#include "BurudogaSearch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "BattleEmulator.h"
#include "lcg.h"

namespace {
	using Clock = std::chrono::steady_clock;
	constexpr int kActionCapacity = 350;
	constexpr uint32_t kNoPath = std::numeric_limits<uint32_t>::max();
	constexpr uint64_t kTurnMask = 0xFFFFF000ULL;
	constexpr int kWidths[] = {192, 512, 1536, 4096, 8192};
	constexpr int kMaximumAttackDamage = BattleEmulator::BARUBOROSU_EQUIPPED_ATK * 105 / 100;
	constexpr int kMinimumBareCritical = BattleEmulator::BARUBOROSU_BARE_HANDS_ATK * 95 / 100;

	uint64_t withTurn(uint64_t state, int turn) {
		return (state & ~kTurnMask) | (static_cast<uint64_t>(turn) << 12);
	}

	uint64_t mix(uint64_t value) {
		value ^= value >> 30;
		value *= 0xbf58476d1ce4e5b9ULL;
		value ^= value >> 27;
		value *= 0x94d049bb133111ebULL;
		return value ^ (value >> 31);
	}

	struct Node {
		uint64_t nowState = 0;
		uint64_t hash = 0;
		int rngPosition = 1;
		int allyHp = 0;
		int allyMp = 0;
		int enemyHp = 0;
		int allyAtk = 0;
		int records = 0;
		int changes = 0;
		uint32_t path = kNoPath;
		int32_t action = 0;
		float score = 0;
	};

	struct PathLink {
		uint32_t parent;
		int32_t action;
	};

	uint64_t stateHash(const Node &node) {
		uint64_t hash = mix(node.nowState ^ static_cast<uint32_t>(node.rngPosition));
		hash ^= mix(static_cast<uint32_t>(node.allyHp)
		            | (static_cast<uint64_t>(static_cast<uint32_t>(node.enemyHp)) << 32));
		hash ^= mix(static_cast<uint32_t>(node.allyMp)
		            | (static_cast<uint64_t>(static_cast<uint32_t>(node.allyAtk)) << 32));
		return hash;
	}

	bool sameState(const Node &left, const Node &right) {
		return left.nowState == right.nowState && left.rngPosition == right.rngPosition
		       && left.allyHp == right.allyHp && left.allyMp == right.allyMp
		       && left.enemyHp == right.enemyHp && left.allyAtk == right.allyAtk
		       && left.records == right.records;
	}

	bool bareCritical(const Player &ally, const BattleEmulator::StepSummary &summary) {
		// In these variants bare normal hits are 1..3, and critical hits are 15..16.
		// Observe the real attack result; do not alter its critical roll or damage.
		return ally.atk == BattleEmulator::BARUBOROSU_BARE_HANDS_ATK
		       && summary.allyAction == BattleEmulator::ATTACK_ALLY
		       && summary.allyDamage >= kMinimumBareCritical;
	}

	struct Replay {
		Player players[2];
		BattleEmulator::StepContext context;
		BattleResult trace;
		int rngPosition = 1;
		int changes = 0;
		int turns = 0;
		bool invalidCritical = false;
	};

	class Search {
	public:
		Search(const Player initialPlayers[2], uint64_t seed, const int32_t fixedActions[350], int fixedTurns)
			: started_(Clock::now()),
			  deadline_(started_ + std::chrono::milliseconds(ActionOptimizer::SearchMilliseconds - 15)),
			  initial_{initialPlayers[0], initialPlayers[1]}, seed_(seed), fixedTurns_(fixedTurns) {
			for (int i = 0; i < fixedTurns && i < kActionCapacity - 1; ++i) {
				fullActions_[i] = fixedActions[i];
			}
			normalDamage_ = std::max(1.0f, (2 * BattleEmulator::BARUBOROSU_EQUIPPED_ATK - initial_[1].def) * 0.25f);
			enemyDamage_ = std::max(1.0f, (2 * initial_[1].atk - initial_[0].def) * 0.25f);
		}

		ActionOptimizer::Result run() {
			if (fixedTurns_ < 0 || fixedTurns_ >= kActionCapacity) {
				result_.exhausted = true;
				return finish();
			}
			lcg::init(seed_, true);
			replay(fixedTurns_);
			root_ = replayNode();
			fallbackAttack_ = exact_.context.genePosition == -1;
			result_.finalAllyHp = root_.allyHp;
			result_.finalEnemyHp = root_.enemyHp;
			if (root_.enemyHp == 0) {
				adopt(0);
				verifyWithMain();
				return finish();
			}
			if (root_.allyHp == 0 || fixedTurns_ == kActionCapacity - 1) {
				result_.exhausted = true;
				return finish();
			}

			while (!outOfTime()) {
				const int pass = result_.passes++;
				const int width = kWidths[std::min(pass, static_cast<int>(std::size(kWidths)) - 1)];
				searchPass(width, pass);
			}
			if (result_.solved) {
				verifyWithMain();
			}
			return finish();
		}

	private:
		Clock::time_point started_;
		Clock::time_point deadline_;
		Player initial_[2];
		uint64_t seed_;
		int fixedTurns_;
		bool fallbackAttack_ = false;
		float normalDamage_ = 1;
		float enemyDamage_ = 1;
		ActionOptimizer::Result result_;
		std::array<int32_t, kActionCapacity> fullActions_{};
		Replay exact_;
		Node root_;
		Node bestTerminal_;
		std::vector<Node> frontier_;
		std::vector<Node> candidates_;
		std::vector<PathLink> paths_;
		std::vector<int> slots_;
		std::vector<uint8_t> selected_;
		std::vector<uint8_t> covered_;

		bool outOfTime() const {
			return Clock::now() >= deadline_;
		}

		ActionOptimizer::Result finish() {
			result_.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
			return std::move(result_);
		}

		void replay(int totalTurns) {
			exact_.players[0] = initial_[0];
			exact_.players[1] = initial_[1];
			exact_.context = {};
			exact_.trace.clear();
			exact_.rngPosition = 1;
			exact_.changes = 0;
			exact_.turns = 0;
			exact_.invalidCritical = false;
			for (int i = 0; i < totalTurns && exact_.players[0].hp != 0 && exact_.players[1].hp != 0; ++i) {
				BattleEmulator::StepSummary summary;
				exact_.context.summary = &summary;
				const int previousAtk = exact_.players[0].atk;
				const auto step = BattleEmulator::StepAction(&exact_.rngPosition, i + 1, fullActions_[i],
					exact_.players, &exact_.trace, nullptr, -1, &exact_.context);
				exact_.changes += previousAtk != exact_.players[0].atk;
				exact_.turns = i + 1;
				exact_.context.nowState = withTurn(exact_.context.nowState, i + 1);
				if (i >= fixedTurns_ && bareCritical(exact_.players[0], summary)) {
					exact_.invalidCritical = true;
				}
				if (step.finished) {
					break;
				}
			}
			exact_.context.summary = nullptr;
		}

		Node replayNode() const {
			Node node;
			node.nowState = exact_.context.nowState;
			node.rngPosition = exact_.rngPosition;
			node.allyHp = exact_.players[0].hp;
			node.allyMp = exact_.players[0].mp;
			node.enemyHp = exact_.players[1].hp;
			node.allyAtk = exact_.players[0].atk;
			node.records = exact_.trace.position;
			node.changes = exact_.changes;
			node.hash = stateHash(node);
			return node;
		}

		bool canImprove(const Node &node) const {
			if (!result_.solved) {
				return true;
			}
			// Every available damaging action is one attack, at most an equipped critical.
			// Nonterminal turns have two actual records; the killing turn needs at least one.
			const int attacks = (node.enemyHp + kMaximumAttackDamage - 1) / kMaximumAttackDamage;
			const int lowerBound = node.records + 2 * attacks - 1;
			return lowerBound < result_.replay.position
			       || (lowerBound == result_.replay.position && node.changes < result_.equipmentChanges);
		}

		float score(const Node &node, int pass) const {
			const float attacks = node.enemyHp / normalDamage_;
			const float health = node.allyHp / enemyDamage_;
			const float heals = node.allyMp / 2;
			const float endurance = health + heals * (30.0f / enemyDamage_ - 1.0f);
			const float deficit = std::max(0.0f, attacks - endurance);
			const int profile = pass % 4;
			const float survival = profile == 1 ? 1.6f : 0.8f;
			const float reserve = profile == 1 ? 0.22f : 0.08f;
			const float changes = profile == 2 ? 0.16f : 0.02f;
			const float jitter = pass == 0 ? 0.0f
				: static_cast<float>(mix(node.hash ^ mix(static_cast<uint64_t>(pass))) & 1023) / 1024.0f
				  * (profile == 3 ? 0.8f : 0.3f);
			return attacks + survival * deficit + 0.5f * std::max(0.0f, 1.5f - health)
			       - reserve * (std::min(health, 4.0f) + heals)
			       + changes * node.changes + jitter;
		}

		void adopt(int futureTurns) {
			result_.solved = true;
			result_.turn = futureTurns;
			result_.replay = exact_.trace;
			result_.equipmentChanges = exact_.changes;
			result_.rngPosition = exact_.rngPosition;
			result_.finalAllyHp = exact_.players[0].hp;
			result_.finalEnemyHp = exact_.players[1].hp;
			for (int i = 0; i < futureTurns; ++i) {
				result_.actions[i] = fullActions_[fixedTurns_ + i];
			}
			result_.actions[futureTurns] = -1;
			bestTerminal_ = replayNode();
		}

		void consider(const Node &candidate, int depth) {
			++result_.winningNodes;
			if (result_.solved && (candidate.records > result_.replay.position
			    || (candidate.records == result_.replay.position && candidate.changes >= result_.equipmentChanges))) {
				return;
			}
			fullActions_[fixedTurns_ + depth - 1] = candidate.action;
			uint32_t path = candidate.path;
			for (int i = depth - 2; i >= 0; --i) {
				if (path == kNoPath) {
					return;
				}
				fullActions_[fixedTurns_ + i] = paths_[path].action;
				path = paths_[path].parent;
			}
			fullActions_[fixedTurns_ + depth] = -1;
			replay(fixedTurns_ + depth);
			++result_.replayChecks;
			const Node actual = replayNode();
			if (exact_.invalidCritical || actual.enemyHp != 0 || !sameState(actual, candidate)
			    || actual.changes != candidate.changes || exact_.turns != fixedTurns_ + depth) {
				return;
			}
			if (!result_.solved || exact_.trace.position < result_.replay.position
			    || (exact_.trace.position == result_.replay.position && exact_.changes < result_.equipmentChanges)) {
				adopt(depth);
			}
		}

		void verifyWithMain() {
			for (int i = 0; i < result_.turn; ++i) {
				fullActions_[fixedTurns_ + i] = result_.actions[i];
			}
			fullActions_[fixedTurns_ + result_.turn] = -1;
			// Main starts from the original world, independently of the search snapshots.
			exact_.players[0] = initial_[0];
			exact_.players[1] = initial_[1];
			exact_.rngPosition = 1;
			exact_.context.nowState = 0;
			exact_.trace.clear();
			lcg::init(seed_, true);
			BattleEmulator::Main(&exact_.rngPosition, fixedTurns_ + result_.turn, fullActions_.data(),
				exact_.players, &exact_.trace, seed_, nullptr, nullptr, -1, &exact_.context.nowState);
			++result_.replayChecks;
			bool matches = sameState(replayNode(), bestTerminal_) && exact_.players[1].hp == 0;
			for (int i = 0; matches && i < exact_.trace.position; ++i) {
				matches = exact_.trace.actions[i] == result_.replay.actions[i]
				          && exact_.trace.damages[i] == result_.replay.damages[i]
				          && exact_.trace.isEnemy[i] == result_.replay.isEnemy[i]
				          && exact_.trace.turns[i] == result_.replay.turns[i]
				          && exact_.trace.initiative[i] == result_.replay.initiative[i]
				          && exact_.trace.ehp[i] == result_.replay.ehp[i]
				          && exact_.trace.ahp[i] == result_.replay.ahp[i]
				          && exact_.trace.amp[i] == result_.replay.amp[i];
			}
			result_.solved = matches;
			if (matches) {
				result_.replay = exact_.trace;
			}
		}

		void offer(Node node, int pass) {
			node.hash = stateHash(node);
			node.score = score(node, pass);
			std::size_t slot = node.hash & (slots_.size() - 1);
			while (slots_[slot] != -1) {
				Node &previous = candidates_[slots_[slot]];
				if (previous.hash == node.hash && sameState(previous, node)) {
					if (node.changes < previous.changes) {
						previous = node;
					}
					return;
				}
				slot = (slot + 1) & (slots_.size() - 1);
			}
			slots_[slot] = static_cast<int>(candidates_.size());
			candidates_.push_back(node);
		}

		void retain(std::size_t index) {
			Node node = candidates_[index];
			paths_.push_back({node.path, node.action});
			node.path = static_cast<uint32_t>(paths_.size() - 1);
			frontier_.push_back(node);
			selected_[index] = 1;
			const uint64_t group = mix(node.nowState ^ static_cast<uint32_t>(node.rngPosition));
			covered_[group & (covered_.size() - 1)] = 1;
		}

		void selectFrontier(int width) {
			std::sort(candidates_.begin(), candidates_.end(), [](const Node &left, const Node &right) {
				if (left.score != right.score) return left.score < right.score;
				if (left.changes != right.changes) return left.changes < right.changes;
				return left.hash < right.hash;
			});
			frontier_.clear();
			selected_.assign(candidates_.size(), 0);
			std::fill(covered_.begin(), covered_.end(), 0);
			const std::size_t limit = std::min(candidates_.size(), static_cast<std::size_t>(width));
			const std::size_t mainQuota = candidates_.size() <= limit ? limit : limit * 3 / 4;
			for (std::size_t i = 0; i < mainQuota; ++i) {
				retain(i);
			}
			// Reserve a quarter of the beam for other RNG/camera continuations.
			for (std::size_t i = mainQuota; i < candidates_.size() && frontier_.size() < limit; ++i) {
				const auto &node = candidates_[i];
				const uint64_t group = mix(node.nowState ^ static_cast<uint32_t>(node.rngPosition));
				if (!covered_[group & (covered_.size() - 1)]) {
					retain(i);
				}
			}
			for (std::size_t i = mainQuota; i < candidates_.size() && frontier_.size() < limit; ++i) {
				if (!selected_[i]) retain(i);
			}
		}

		void searchPass(int width, int pass) {
			frontier_.clear();
			frontier_.reserve(width);
			frontier_.push_back(root_);
			paths_.clear();
			candidates_.reserve(width * 5);
			std::size_t slotCount = 1;
			while (slotCount < static_cast<std::size_t>(width) * 10) slotCount *= 2;
			slots_.resize(slotCount);
			covered_.resize(slotCount / 2);
			const int futureLimit = kActionCapacity - 1 - fixedTurns_;
			for (int depth = 1; depth <= futureLimit && !frontier_.empty(); ++depth) {
				if (outOfTime()) return;
				result_.maxDepth = std::max(result_.maxDepth, depth);
				candidates_.clear();
				std::fill(slots_.begin(), slots_.end(), -1);
				for (const Node &parent : frontier_) {
					if (!canImprove(parent)) continue;
					const int heldEquipment = parent.allyAtk == BattleEmulator::BARUBOROSU_BARE_HANDS_ATK
						? BattleEmulator::ACTION_BARE_HANDS : 0;
					// Equipment only changes an attack. Keep it through non-attacking turns.
					const int32_t actions[] = {
						BattleEmulator::ATTACK_ALLY,
						BattleEmulator::ATTACK_ALLY | BattleEmulator::ACTION_BARE_HANDS,
						BattleEmulator::HEAL | heldEquipment,
						BattleEmulator::DEFENCE | heldEquipment,
						BattleEmulator::FLEE_ALLY | heldEquipment,
					};
					for (int actionIndex = 0; actionIndex < (fallbackAttack_ ? 1 : 5); ++actionIndex) {
						const int32_t action = actions[actionIndex];
						if ((action & BattleEmulator::ACTION_ID_MASK) == BattleEmulator::HEAL && parent.allyMp < 2) continue;
						if ((result_.nodesVisited & 127) == 0 && outOfTime()) return;
						Player players[2] = {initial_[0], initial_[1]};
						players[0].hp = parent.allyHp;
						players[0].mp = parent.allyMp;
						players[0].atk = parent.allyAtk;
						players[1].hp = parent.enemyHp;
						BattleEmulator::StepSummary summary;
						BattleEmulator::StepContext context;
						context.nowState = parent.nowState;
						context.genePosition = fallbackAttack_ ? -1 : 0;
						context.summary = &summary;
						Node child = parent;
						BattleEmulator::StepAction(&child.rngPosition, fixedTurns_ + depth, action,
							players, nullptr, nullptr, -2, &context);
						++result_.nodesVisited;
						if (bareCritical(players[0], summary)) continue;
						child.nowState = withTurn(context.nowState, fixedTurns_ + depth);
						child.allyHp = players[0].hp;
						child.allyMp = players[0].mp;
						child.enemyHp = players[1].hp;
						child.allyAtk = players[0].atk;
						child.records += (summary.enemyAction != 0) + (summary.allyAction != 0);
						child.changes += parent.allyAtk != child.allyAtk;
						child.action = action;
						if (child.enemyHp == 0) {
							consider(child, depth);
						} else if (child.allyHp != 0 && depth < futureLimit && canImprove(child)) {
							offer(child, pass);
						}
					}
				}
				if (outOfTime()) return;
				selectFrontier(width);
			}
		}
	};
}

ActionOptimizer::Result BurudogaSearch::Run(const Player initialPlayers[2], uint64_t seed,
	                                        const int32_t fixedActions[350], int fixedTurns) {
	// Keep replay records and beam storage off the WebAssembly stack.
	return std::make_unique<Search>(initialPlayers, seed, fixedActions, fixedTurns)->run();
}
