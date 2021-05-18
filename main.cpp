#include <iostream>

enum class State {
		IDLE_1,
};

State readState() {
	FILE* state = fopen("/root/state", "r");
	if (not state) {
		return State::IDLE_1;
	}

	int stateInt = 0;
	fscanf(state, "%d", &stateInt);
	fclose(state);
	return static_cast<State>(stateInt);
}

void writeState(State state) {
	FILE* stateFile = fopen("/root/state", "w");
	if (not stateFile) {
		return;
	}
	fprintf(stateFile, "%d\n", state);
}

int main() {
	FILE* pipe = fopen("/tmp/irrigation.pipe", "r");

	return 0;
}
