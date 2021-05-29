#include <iostream>

class State {
	public:
		explicit State(int theState) : state(theState) {}

		const int state;

		int getPosition() const {
			return state & ~FLAG_ON;
		}
		int getNextPosition() {
			return (getPosition() + 1) % POSITION_COUNT;
		}
		int isOn() {
			return (state & FLAG_ON) != 0;
		}

		State turnOn() {
			return State(state | FLAG_ON);
		}
		State turnOff() {
			return State(getNextPosition());
		}

		static constexpr int FLAG_ON = 0x8000;
		static constexpr int POSITION_COUNT = 6;
};

State readState() {
	FILE* state = fopen("/root/state", "r");
	if (not state) {
		return State(0);
	}

	int stateInt = 0;
	fscanf(state, "%d", &stateInt);
	fclose(state);
	return State(stateInt);
}

void writeState(State state) {
	FILE* stateFile = fopen("/root/state", "w");
	if (not stateFile) {
		return;
	}
	fprintf(stateFile, "%d\n", state.state);
	fclose(stateFile);
}

void writeGpio(bool on) {
	
}

State turnOn(State state) {
	auto nextState = state.turnOn();
	writeState(nextState);
	writeGpio(true);
	return nextState;
}

State turnOff(State state) {
	auto nextState = state.turnOff();
	writeState(nextState);
	writeGpio(false);
	return nextState;
}

int main() {
	FILE* pipe = fopen("/tmp/irrigation.pipe", "r");

	auto state = readState();
	if (state.isOn()) {

	}

	fd_set rfds;
	struct timeval tv;
	int retval;

	FD_ZERO(&rfds);
	FD_SET(fileno(pipe), &rfds);

	/* Wait up to five seconds. */

	tv.tv_sec = 5;
	tv.tv_usec = 0;

	retval = select(1, &rfds, NULL, NULL, &tv);
	/* Don't rely on the value of tv now! */

	if (retval == -1)
		perror("select()");
	else if (retval)
		printf("Data is available now.\n");
		/* FD_ISSET(0, &rfds) will be true. */
	else
		printf("No data within five seconds.\n");


	return 0;
}
