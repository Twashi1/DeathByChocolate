#include <iostream>
#include <array>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <format>

#define __DEBUG
#define __ENABLE_TRANSPOSITIONS
static bool PRINTING_ALL = true;
#define Log(msg, ...) if (PRINTING_ALL) std::cout << std::format(msg, __VA_ARGS__) << std::endl

// TODO: alpha/beta pruning

typedef uint16_t bar_t;
typedef uint64_t hash_t;

struct Move {
	enum Direction {
		HORIZONTAL,
		VERTICAL
	};

	Direction dir;
	bar_t location;

	Move(const Direction& dir, bar_t location)
		: dir(dir), location(location) {}
};

struct ChocolateBar {
	bar_t rows;
	bar_t columns;

	bar_t poison_row;
	bar_t poison_column;

	ChocolateBar(bar_t _columns, bar_t _rows, bar_t _poison_column, bar_t _poison_row)
		: rows(_rows), columns(_columns), poison_row(_poison_row), poison_column(_poison_column) {}

	hash_t PositionHash() {
		hash_t hash = 0;

		hash |= (hash_t)rows;
		hash |= (hash_t)columns			<< sizeof(bar_t) * 8;
		hash |= (hash_t)poison_row		<< sizeof(bar_t) * 8 * 2;
		hash |= (hash_t)poison_column	<< sizeof(bar_t) * 8 * 3;

		return hash;
	}

	bool CheckLost() const {
#ifdef __DEBUG
		if (rows <= 1 && columns <= 1) {
			// Check that the only square left is the poison square
			if (!(poison_row == 0 && poison_column == 0)) {
				Log("Poison square was not left!");
			}

			return true;
		}

		return false;
#else
		return rows <= 1 && columns <= 1;
#endif
	}

	void SplitVertical(bar_t column) {
		// Indicates the poison is to the right of the column we're splitting
		if (poison_column >= column) {
			poison_column -= column;

			columns -= column;
		}
		// Poison is to the left of the column we're splitting
		else {
			columns = column;
		}
	}

	void SplitHorizontal(bar_t row) {
		// Indicates the poison is above the row we're splitting
		if (poison_row >= row) {
			poison_row -= row;

			rows -= row;
		}
		// Poison is below the column we're splitting
		else {
			rows = row;
		}
	}

	std::vector<Move> GetValidMoves() const {
		std::vector<Move> moves;

		moves.reserve(rows * columns);

		for (int row = 1; row < rows; row++) {
			moves.emplace_back(Move::Direction::HORIZONTAL, row);
		}

		for (int column = 1; column < columns; column++) {
			moves.emplace_back(Move::Direction::VERTICAL, column);
		}

		return moves;
	}

	void MakeMove(const Move& move) {
		if (move.dir == Move::Direction::VERTICAL) {
			SplitVertical(move.location);
		}
		else {
			SplitHorizontal(move.location);
		}
	}

	void Print() const {
		for (bar_t row = 0; row < rows; row++) {
			for (bar_t column = 0; column < columns; column++) {
				if (row == poison_row && column == poison_column) {
					std::cout << "P";
				}
				else {
					std::cout << "#";
				}
			}

			std::cout << std::endl;
		}

		std::cout << std::endl;
	}

	bool CheckValidMove(const Move& move) {
		if (move.location < 1) {
			return false;
		}

		if (move.dir == Move::Direction::VERTICAL) {
			if (move.location >= columns) {
				return false;
			}
		}
		else  {
			if (move.location >= rows) {
				return false;
			}
		}

		return true;
	}
};

struct TranspositionTable {
	typedef std::size_t index_t;

	struct Entry {
		// To get this hash, we'd have to have 65535 rows, 65535 columns, and the poison in an OOB square
		static const hash_t INVALID_HASH = 0xffffffff;

		hash_t position_hash = INVALID_HASH;
		float score = 0.0f;

		inline bool isInvalid() { return position_hash == INVALID_HASH; }
	};

	std::size_t table_size; // MAX SIZE
	std::size_t current_size = 0; // ACTUAL SIZE
	Entry* data;

	TranspositionTable(std::size_t table_size)
		: table_size(table_size)
	{
		data = new Entry[table_size];

		// Fill with empty
		std::fill_n(data, table_size, Entry());
	}

	~TranspositionTable()
	{
		delete[] data;
	}

	index_t GetIndex(hash_t position_hash) {
		return position_hash % table_size;
	}

	void Reset() {
		std::fill_n(data, table_size, Entry());
	}

	Entry& AddEntry(hash_t position_hash, float score) {
		if (current_size == table_size) {
			Log("[WARN] Table completely filled! Ignoring call");
			
			return data[0];
		}

		index_t index = GetIndex(position_hash);

		Entry* pending_entry = &data[index];

		static const int MAX_ATTEMPTS = 100;
		int attempts = 0;

		// This entry already exists
		while ((!pending_entry->isInvalid()) && attempts < MAX_ATTEMPTS) {
			// Increment attempts
			++attempts;
			// Increment index (using skip factor of 1)
			index = (index + 1) % table_size;

			// NOTE: could increment the pointer instead, but might lead to some difficult errors
			pending_entry = &data[index];
		}

		if (attempts >= MAX_ATTEMPTS) {
			Log("Failed lookup from too many attempts");
		}

		// Found an empty entry, so set the position hash and score
		pending_entry->position_hash = position_hash;
		pending_entry->score = score;

		// Increment current size
		++current_size;

		// Return reference to that entry
		return *pending_entry;
	}

	Entry Lookup(hash_t position_hash) {
		if (current_size == table_size) {
			Log("[WARN] Table completely filled! Ignoring call");

			return Entry();
		}

		index_t index = GetIndex(position_hash);

		Entry* test_entry = &data[index];

		while (test_entry->position_hash != position_hash) {
			// We found an empty entry
			if (test_entry->position_hash == Entry::INVALID_HASH) {
				return Entry();
			}

			index = (index + 1) % table_size;
			
			test_entry = &data[index];
		}

		return *test_entry;
	}

	// Delete copy operators
	TranspositionTable(const TranspositionTable&) = delete;
	TranspositionTable& operator=(const TranspositionTable&) = delete;
};

std::string ReprMove(const Move& move) {
	std::stringstream ss;

	if (move.dir == Move::Direction::HORIZONTAL) {
		ss << "Horizontal split at: ";
	}
	else {
		ss << "Vertical split at: ";
	}

	ss << move.location;

	return ss.str();
}

float Evaluate(ChocolateBar bar, int& positions_searched, TranspositionTable& table) {
	std::vector<Move> moves = bar.GetValidMoves();

	// Next person to move loses, so return a score of 1
	if (moves.empty()) {
		return 1;
	}
	else {
		float max_score = FLT_MAX;
		float move_count = moves.size();

		// Iterate possible moves
		for (const Move& move : moves) {
			ChocolateBar test_bar = bar;

			// Make move on our test bar
			test_bar.MakeMove(move);

			// Check if this state has already been evaluated
#ifdef __ENABLE_TRANSPOSITIONS
			hash_t position_hash = test_bar.PositionHash();
			TranspositionTable::Entry entry = table.Lookup(position_hash);

			// Means this position has been looked up before
			if (!entry.isInvalid()) {
				// So add score for that position to our total
				max_score = std::min(max_score, entry.score);
			}
			// Otherwise evaluate state as normal, and add state to table
			else {
				// Get score for this state
				float position_score = -Evaluate(test_bar, ++positions_searched, table);

				// Add to lookup table
				table.AddEntry(position_hash, position_score);

				max_score = std::min(max_score, position_score);
			}

#else
			// Get score for next position
			float position_score = -Evaluate(test_bar, ++positions_searched, table);

			// Add score to our total
			total_score += position_score;
#endif
		}

		// Calculate average score
		// float average = total_score / move_count;

		// Return average score
		return max_score;
	}
}

Move GetAIMove(ChocolateBar bar, TranspositionTable& table, float* move_score = nullptr) {
	std::vector<Move> possible_moves = bar.GetValidMoves();

	int total_searched = 0;

	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	float best_move_score = -FLT_MAX;
	Move* best_move = nullptr;

	if (move_score != nullptr) {
		*move_score = -1.0f;
	}

	for (Move& move : possible_moves) {
		ChocolateBar test_bar = bar;

		test_bar.MakeMove(move);

		float score = Evaluate(test_bar, total_searched, table);

		if (score > best_move_score) {
			best_move_score = score;
			best_move = &move;

			if (move_score != nullptr) { *move_score = best_move_score; }
		}

		// This move will lead to a guaranteed win, so don't process any more
		if (score == 1.0f) {
			Log("Found guaranteed win");

			break;
		}
	}

	if (best_move == nullptr) {
		Log("ERROR! No AI move found!");

		return Move(Move::VERTICAL, 0);
	}
	else {
		std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

		float elapsed_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

		Log("Searched {} positions in {}ms", total_searched, elapsed_time / 1000.0f);

		return *best_move;
	}
}

Move GetPlayerMove(ChocolateBar bar) {
	Move pending_move = Move(Move::Direction::VERTICAL, 0xffff);

	while (!bar.CheckValidMove(pending_move)) {
		std::cout << "What direction would you like to split in? (v/h) ";
		char direction;
		std::cin >> direction;

		std::cout << std::endl << "What location would you like to split at? ";
		bar_t location;
		std::cin >> location;

		if (direction == 'v') {
			pending_move.dir = Move::Direction::VERTICAL;
		}
		else if (direction == 'h') {
			pending_move.dir = Move::Direction::HORIZONTAL;
		}
		else {
			std::cout << "Invalid direction" << std::endl;

			continue;
		}

		pending_move.location = location;

		if (!bar.CheckValidMove(pending_move)) {
			std::cout << "Invalid move!" << std::endl;
		}
	}

	return pending_move;
}

enum MOVE_ORDER {
	AI_MOVE_FIRST,
	AI_MOVE_SECOND
};

MOVE_ORDER PlayerSelectMoveOrder() {
	std::cout << "Would you like to move first or second? (1/2): ";
	int order;
	std::cin >> order;

	if (order == 1) {
		return AI_MOVE_SECOND;
	}
	else {
		return AI_MOVE_FIRST;
	}
}

// AI will calculate whether it should move first or second
MOVE_ORDER GetAIMoveOrder(ChocolateBar bar, TranspositionTable& table) {
	// NOTE: don't think we can reuse transposition table for this, maybe if you invert the values?
	TranspositionTable first_table(table.table_size);
	TranspositionTable second_table(table.table_size);
	
	/* -- CALCULATING SCORE WHEN MOVING FIRST -- */
	float first_score = 0.0f;
	GetAIMove(bar, first_table, &first_score);

	float second_score = first_score * -1.0f;

	Log("First score: {}, Second score: {}", first_score, second_score);

	if (first_score > second_score) {
		Log("AI determined going first was beneficial in this position");

		return AI_MOVE_FIRST;
	}
	else if (second_score > first_score) {
		Log("AI determined going second was beneficial in this position");

		return AI_MOVE_SECOND;
	}
	else {
		Log("[ERROR] AI determined no difference, defaulting to first");

		return AI_MOVE_FIRST;
	}
}

bool PlayerTurn(ChocolateBar& bar) {
	std::cout << "Human's turn!" << std::endl;
	bar.Print();
	Move player_move = GetPlayerMove(bar);
	std::cout << ReprMove(player_move) << std::endl;
	bar.MakeMove(player_move);

	if (bar.CheckLost()) {
		std::cout << "AI lost!" << std::endl;

		return true;
	}

	return false;
}

bool AITurn(ChocolateBar& bar, TranspositionTable& table) {
	std::cout << "AI's turn!" << std::endl;
	bar.Print();
	Move ai_move = GetAIMove(bar, table);
	std::cout << ReprMove(ai_move) << std::endl;
	bar.MakeMove(ai_move);

	if (bar.CheckLost()) {
		std::cout << "Player lost!" << std::endl;

		return true;
	}

	return false;
}

void PlayAgainstAI() {
	ChocolateBar bar(5, 3, 2, 1);
	// 100k possible entries
	TranspositionTable table(100000);

	bool AIMovesFirst = GetAIMoveOrder(bar, table) == AI_MOVE_FIRST;

	Log("\n<--- GAME STARTING --->");

	while (!bar.CheckLost()) {
		// TODO: very bad
		if (AIMovesFirst) {
			if (AITurn(bar, table)) { break; }
			if (PlayerTurn(bar)) { break; }
		}
		else {
			if (PlayerTurn(bar)) { break; }
			if (AITurn(bar, table)) { break; }
		}
	}
}

void AITestBars() {
	const int max_size = 11;

	int amount_first = 0;
	int amount_second = 0;

	float total_bars_gen = (max_size * (max_size + 1) / 2) * (max_size * (max_size + 1) / 2);
	std::cout << "Total bars: " << total_bars_gen << std::endl;
	int bars_counter = 0;
	float last_percent = 0;

	for (int rows = 1; rows <= max_size; rows++) {
		for (int columns = 1; columns <= max_size; columns++) {
			for (int prows = 0; prows < rows; prows++) {
				for (int pcolumns = 0; pcolumns < columns; pcolumns++) {
					ChocolateBar bar(columns, rows, pcolumns, prows);

					TranspositionTable table(100000);
					MOVE_ORDER ai_move_order = GetAIMoveOrder(bar, table);

					switch (ai_move_order) {
					case AI_MOVE_FIRST: ++amount_first; break;
					case AI_MOVE_SECOND: ++amount_second; break;
					default: std::cout << "INVALID MOVE ORDER" << std::endl; break;
					}
					
					++bars_counter;

					float current_percent = bars_counter / total_bars_gen;

					if (current_percent - last_percent > 0.01f) {
						last_percent = current_percent;

						PRINTING_ALL = true;

						Log("{}% Done", current_percent * 100.0f);

						PRINTING_ALL = false;
					}
				}
			}
		}
	}

	PRINTING_ALL = true;

	Log("Went first {} times, went second {} times", amount_first, amount_second);
	
	float sum_times = amount_first + amount_second;
	float first_percent = (float)amount_first / sum_times;
	float second_percent = (float)amount_second / sum_times;

	Log("Went first {}%, second {}%", first_percent * 100.0f, second_percent * 100.0f);
	Log("Evaluated {} bars total", bars_counter);
}

void GenerateWinMap(int columns, int rows) {
	TranspositionTable table(100000);

	PRINTING_ALL = false;

	for (int prow = 0; prow < rows; prow++) {
		for (int pcolumn = 0; pcolumn < columns; pcolumn++) {
			MOVE_ORDER order = GetAIMoveOrder(ChocolateBar(columns, rows, pcolumn, prow), table);

			if (order == AI_MOVE_FIRST) {
				std::cout << "#";
			}
			else {
				std::cout << "-";
			}

			table.Reset();
		}

		std::cout << std::endl;
	}
}

#define __WINMAP

int main(void) {
#ifdef __WINMAP
	std::cout << "Rows: ";
	int rows;
	std::cin >> rows;
	std::cout << std::endl << "Columns: ";
	int columns;
	std::cin >> columns;
	std::cout << std::endl;

	GenerateWinMap(columns, rows);
#else
	PlayAgainstAI();
	AITestBars();
#endif
}
