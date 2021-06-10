#include <iostream>
#include <cstring>
#include <sys/syslog.h>
#include "Util.h"
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <sstream>
#include <array>
#include <unistd.h>

using namespace std;

static constexpr int VALVE_COUNT = 6;
static constexpr int MAX_VALVE_COUNT = 6;
static constexpr int SLEEP_SECONDS_AFTER_TURN_OFF = 45;
static constexpr int SLEEP_SECONDS_AFTER_TURN_ON = 10;

class State {
	public:
		explicit State(int theState) : state(theState) {}

		int state;

		int getPosition() const {
			return state & ~FLAG_ON;
		}
		int getNextPosition() {
			return (getPosition() + 1) % VALVE_COUNT;
		}
		int isOn() {
			return (state & FLAG_ON) != 0;
		}

		void turnOn() {
			state |= FLAG_ON;
		}
		void turnOff() {
			state = getNextPosition();
		}

		static constexpr int FLAG_ON = 0x8000;
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

void sleepAtLeast(int seconds) {
	struct timespec ts, rem;
	ts.tv_sec = seconds;
	ts.tv_nsec = 0;
	while (nanosleep(&ts, &rem) && errno == EINTR) {
		ts = rem;
	}
}

void writeState(State state) {
	FILE* stateFile = fopen("/root/state", "w");
	if (not stateFile) {
		return;
	}
	fprintf(stateFile, "%d\n", state.state);
	fclose(stateFile);
}

/*
        let valuePath = gpioBasePath + "gpio" + pin + "/value";
        let directionPath = gpioBasePath + "gpio" + pin + "/direction";
        if (!fs.existsSync(valuePath)) {
            fs.writeFileSync(gpioBasePath + 'export', "" + pin);
        }
        if (fs.readFileSync(directionPath).toString() != "out\n") {
            console.log("updated direction of " + pin);
            fs.writeFileSync(directionPath, "out");
        }
        let value = state ? "0\n" : "1\n";

        if (fs.readFileSync(valuePath).toString() != value || force) {
            console.log("updated value of " + pin);
            fs.writeFileSync(valuePath, value);
            postdata.push([dateformat(ts, "yyyy-mm-dd HH:MM:ss"), id, (state ? 1 : 0) | (auto << 1), relais]);
        }
    } else {
      postdata.push([dateformat(ts, "yyyy-mm-dd HH:MM:ss"), id, (state ? 1 : 0), relais]);
    }
}
 */

const char gpioBasePath[] = "/sys/class/gpio/";
const char directionOut[] = "out\n";

void writeGpio(int pin, bool on) {
	const auto valuePath = format("%s/gpio%d/value", gpioBasePath, pin);
	const auto directionPath = format("%s/gpio%d/direction", gpioBasePath, pin);
	if (not fileExists(valuePath.c_str())) {
		writeFile(format("%s/export", gpioBasePath).c_str(), format("%d", pin).c_str());
	}
	if (readFileMax1024byte(directionPath.c_str()) != directionOut) {
		writeFile(directionPath.c_str(), directionOut);
	}
	const char* value = on ? "0\n" : "1\n";

	if (readFileMax1024byte(valuePath.c_str()) != value) {
		writeFile(valuePath.c_str(), value);
	}
}

void writeGpio(bool on) {
	const int IRRIGATION_GPIO = 198;
	writeGpio(IRRIGATION_GPIO, on);
}

void turnOn(State& state, double minutes) {
	syslog(LOG_INFO, "Turn on: %d for %.1f min", state.getPosition(), minutes);
	state.turnOn();
	writeState(state);
	writeGpio(true);
}

void turnOff(State& state, double minutes) {
	syslog(LOG_INFO, "Turn off: %d for %.1f min", state.getPosition(), minutes);
	state.turnOff();
	writeState(state);
	writeGpio(false);
}

enum class CommandType {
		QUIT,
		STOP,
		IRRIGATION_CYCLE,
};

struct Command {
	CommandType type;
	array<int, MAX_VALVE_COUNT> duration;
};

class Context {
	public:
		explicit Context(State theState)
			: state(theState) {}

		std::mutex mutex;
		std::unique_ptr<Command> command;
		std::condition_variable cond;
		State state;

		bool hasCommand() const {
			return command.get() != nullptr;
		}

		std::unique_ptr<Command> consumeCommand() {
			return std::move(command);
		}

		void postCommand(Command cmd) {
			{
				std::unique_lock<std::mutex> lk(mutex);
				command = std::make_unique<Command>(cmd);
			}
			cond.notify_all();
		}
};

void open_for(Context* context, int duration_in_seconds) {
	turnOn(context->state, static_cast<double>(duration_in_seconds) / 60.0);
	sleepAtLeast(SLEEP_SECONDS_AFTER_TURN_ON);

	const int time_to_sleep = duration_in_seconds - SLEEP_SECONDS_AFTER_TURN_ON;
	if (time_to_sleep > 0) {
		std::unique_lock<std::mutex> lk(context->mutex);
		context->cond.wait_for(lk,
							   std::chrono::seconds(time_to_sleep),
							   [context] { return context->hasCommand(); });
	}
	turnOff(context->state, static_cast<double>(SLEEP_SECONDS_AFTER_TURN_OFF));
	sleepAtLeast(SLEEP_SECONDS_AFTER_TURN_OFF);
}

void goto_valve(int position, Context* context) {
	while (context->state.getPosition() != position) {
		open_for(context, SLEEP_SECONDS_AFTER_TURN_ON);
	}
}

void do_cycle(Context* context, const Command& command) {
	goto_valve(0, context);
	for (int i=0; i<VALVE_COUNT; ++i) {
		open_for(context, command.duration[i]);
	}
}

void worker(Context* context) {

	std::unique_ptr<Command> nextCommand;
	bool run = true;
	do {
		{
			std::unique_lock<std::mutex> lk(context->mutex);
			context->cond.wait(lk, [context] { return context->hasCommand(); });
			nextCommand = context->consumeCommand();
		}

		switch (nextCommand->type) {
			case CommandType::QUIT:
				run = false;
				break;
			case CommandType::STOP:
				break;
			case CommandType::IRRIGATION_CYCLE:
				do_cycle(context, *nextCommand);
				break;
		}
	} while (run);
}

const char pipeName[] = "/tmp/irrigation.pipe";

void signalHandler(int signal) {
	FILE* f = fopen(pipeName, "w");
	fprintf(f, "quit\n");
	fclose(f);
}

deque<string> split(const char* s, char delim) {
	deque<string> result;
	stringstream ss (s);
	string item;

	while (getline (ss, item, delim)) {
		result.push_back (item);
	}

	return result;
}

int main() {

	Context context(readState());
	if (context.state.isOn()) {
		turnOff(context.state);
	}

	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	std::thread workerThread(worker, &context);

	mkfifo(pipeName, 0666);
	FILE* pipe = fopen(pipeName, "wr+");

	do {
		char buffer[1024];
		while (fgets(buffer, 1024, pipe)) {
			syslog(LOG_INFO, "command: %s", buffer);
			if (buffer[0] == '\0') {
				continue;
			}
			auto len = strlen(buffer);
			if (buffer[len-1] == '\n') buffer[len-1] = '\0';
			auto parts = split(buffer, ' ');
			if (parts.empty()) {
				continue;
			}
			if (not strcasecmp(parts[0].c_str(), "quit")) {
				context.postCommand({CommandType::QUIT});
				workerThread.join();
				return 0;
			}
			if (not strcasecmp(parts[0].c_str(), "stop")) {
				context.postCommand({CommandType::STOP});
			}
			if (not strcasecmp(parts[0].c_str(), "cycle")) {
				if (parts.size() < VALVE_COUNT + 1) {
					syslog(LOG_ERR, "Need cycle times for %d valves", VALVE_COUNT);
				} else {
					Command cmd = {CommandType::IRRIGATION_CYCLE};
					for (int i=1; i<parts.size(); ++i) {
						cmd.duration[i-1] = strtod(parts[i].c_str(), nullptr) * 60;
					}
					context.postCommand(cmd);
				}
			}
			if (not strcasecmp(parts[0].c_str(), "valve")) {
				if (parts.size() < 3) {
					syslog(LOG_ERR, "Need valve and cycle time");
				} else {
					Command cmd = {CommandType::IRRIGATION_CYCLE};
					fill(cmd.duration.begin(), cmd.duration.end(), 0);
					int valve = strtol(parts[1].c_str(), nullptr, 10);
					if (valve < 0 || valve >= VALVE_COUNT) {
						syslog(LOG_ERR, "Invalid valve %d (max: %d)", valve, VALVE_COUNT);
					} else {
						cmd.duration[valve] = strtod(parts[2].c_str(), nullptr) * 60;
						context.postCommand(cmd);
					}
				}
			}
			if (not strcasecmp(parts[0].c_str(), "status")) {
				syslog(LOG_INFO, "valve is: %d, %s", context.state.getPosition(), context.state.isOn() ? "on" : "off");
			}
		}

	} while (true);

}
