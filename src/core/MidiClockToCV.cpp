#include <list>
#include <algorithm>
#include "rtmidi/RtMidi.h"
#include "core.hpp"
#include "MidiInterface.hpp"
#include "dsp/digital.hpp"

using namespace rack;

struct MIDIClockToCVInterface : MidiIO, Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		CLOCK1_PULSE,
		CLOCK2_PULSE,
		CLOCK_START_PULSE,
		CLOCK_STOP_PULSE,
		NUM_OUTPUTS
	};

	int clock1ratio = 0;
	int clock2ratio = 0;

	PulseGenerator clock1Pulse;
	PulseGenerator clock2Pulse;
	PulseGenerator clockStartPulse;
	PulseGenerator clockStopPulse;
	bool tick = false;
	bool running = false;
	bool start = false;
	bool stop = false;

	MIDIClockToCVInterface() : MidiIO(), Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS) {
		(dynamic_cast<RtMidiIn *>(rtMidi))->ignoreTypes(true, false);
	}

	~MIDIClockToCVInterface() {
		setPortId(-1);
	}

	void step();

	void processMidi(std::vector<unsigned char> msg);

	virtual void resetMidi();

	virtual json_t *toJson() {
		json_t *rootJ = json_object();
		addBaseJson(rootJ);
		json_object_set_new(rootJ, "clock1ratio", json_integer(clock1ratio));
		json_object_set_new(rootJ, "clock2ratio", json_integer(clock2ratio));
		return rootJ;
	}

	virtual void fromJson(json_t *rootJ) {
		baseFromJson(rootJ);
		json_t *c1rJ = json_object_get(rootJ, "clock1ratio");
		if (c1rJ) {
			clock1ratio = json_integer_value(c1rJ);
		}

		json_t *c2rJ = json_object_get(rootJ, "clock2ratio");
		if (c2rJ) {
			clock2ratio = json_integer_value(c2rJ);
		}
	}

	virtual void initialize() {
		setPortId(-1);
	}

};

void MIDIClockToCVInterface::step() {
	static int c_bar = 0;
	static float trigger_length = 0.05;

	/* Note this is in relation to the Midi clock's Tick (6x per 16th note).
	 * Therefore, e.g. the 2:3 is calculated:
	 *
	 * 24 (Ticks per quarter note) * 2 / 3 = 16
	 *
	 * Implying that every 16 midi clock ticks we need to send a pulse
	 * */
	static int ratios[] = {6, 8, 12, 16, 24, 32, 48, 96, 192};

	if (rtMidi->isPortOpen()) {
		std::vector<unsigned char> message;

		// midiIn->getMessage returns empty vector if there are no messages in the queue

		dynamic_cast<RtMidiIn *>(rtMidi)->getMessage(&message);
		while (message.size() > 0) {
			processMidi(message);
			dynamic_cast<RtMidiIn *>(rtMidi)->getMessage(&message);
		}

	}

	if (start) {
		clockStartPulse.trigger(trigger_length);
		start = false;
		c_bar = 0;
	}

	if (stop) {
		clockStopPulse.trigger(trigger_length);
		stop = false;
		clock1Pulse.time = 0.0;
		clock1Pulse.pulseTime = 0.0;
		clock2Pulse.time = 0.0;
		clock2Pulse.pulseTime = 0.0;
	}

	if (tick) {
		tick = false;

		/* Note: At least for my midi clock, the clock ticks are sent
		 * even if the midi clock is stopped.
		 * Therefore, we need to keep track of ticks even when the clock
		 * is stopped. Otherwise we can run into weird timing issues.
		*/
		if (running) {
			if (c_bar % ratios[clock1ratio] == 0) {
				clock1Pulse.trigger(trigger_length);
			}

			if (c_bar % ratios[clock2ratio] == 0) {
				clock2Pulse.trigger(trigger_length);
			}
		}

		c_bar++;

		// One "midi bar" = 4 whole notes = (6 ticks per 16th) 6 * 16 *4 = 384
		if (c_bar >= 384) {
			c_bar = 0;
		}
	}


	bool pulse = clock1Pulse.process(1.0 / gSampleRate);
	outputs[CLOCK1_PULSE].value = pulse ? 10.0 : 0.0;

	pulse = clock2Pulse.process(1.0 / gSampleRate);
	outputs[CLOCK2_PULSE].value = pulse ? 10.0 : 0.0;

	pulse = clockStartPulse.process(1.0 / gSampleRate);
	outputs[CLOCK_START_PULSE].value = pulse ? 10.0 : 0.0;

	pulse = clockStopPulse.process(1.0 / gSampleRate);
	outputs[CLOCK_STOP_PULSE].value = pulse ? 10.0 : 0.0;

}

void MIDIClockToCVInterface::resetMidi() {
	outputs[CLOCK1_PULSE].value = 0.0;
}

void MIDIClockToCVInterface::processMidi(std::vector<unsigned char> msg) {

	switch (msg[0]) {
		case 0xfa:
			start = true;
			running = true;
			break;
		case 0xfc:
			stop = true;
			running = false;
			break;
		case 0xf8:
			tick = true;
			break;
	}


}

struct ClockRatioItem : MenuItem {
	int ratio;
	int *clockRatio;

	void onAction() {
		*clockRatio = ratio;
	}
};

struct ClockRatioChoice : ChoiceButton {
	int *clockRatio;
	const std::vector<std::string> ratioNames = {"Sixteenth note (1:4 ratio)", "Eighth note triplet (1:3 ratio)",
												 "Eighth note (1:2 ratio)", "Quarter note triplet (2:3 ratio)",
												 "Quarter note (tap speed)", "Half note triplet (4:3 ratio)",
												 "Half note (2:1 ratio)", "Whole note (4:1 ratio)",
												 "Two whole notes (8:1 ratio)"};

	const std::vector<std::string> ratioNames_short = {"1:4 ratio", "1:3 ratio", "1:2 ratio", "2:3 ratio", "1:1 ratio",
													   "4:3", "2:1 ratio", "4:1 ratio", "8:1 ratio"};

	void onAction() {
		Menu *menu = gScene->createMenu();
		menu->box.pos = getAbsolutePos().plus(Vec(0, box.size.y));
		menu->box.size.x = box.size.x;

		for (int ratio = 0; ratio < ratioNames.size(); ratio++) {
			ClockRatioItem *clockRatioItem = new ClockRatioItem();
			clockRatioItem->ratio = ratio;
			clockRatioItem->clockRatio = clockRatio;
			clockRatioItem->text = ratioNames[ratio];
			menu->pushChild(clockRatioItem);
		}
	}

	void step() {
		text = ratioNames_short[*clockRatio];
	}
};

MIDIClockToCVWidget::MIDIClockToCVWidget() {
	MIDIClockToCVInterface *module = new MIDIClockToCVInterface();
	setModule(module);
	box.size = Vec(15 * 9, 380);

	{
		Panel *panel = new LightPanel();
		panel->box.size = box.size;
		addChild(panel);
	}

	float margin = 5;
	float labelHeight = 15;
	float yPos = margin;

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x - 30, 365)));

	{
		Label *label = new Label();
		label->box.pos = Vec(box.size.x - margin - 7 * 15, margin);
		label->text = "MIDI Clock to CV";
		addChild(label);
		yPos = labelHeight * 2;
	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "MIDI Interface";
		addChild(label);
		yPos += labelHeight + margin;

		MidiChoice *midiChoice = new MidiChoice();
		midiChoice->midiModule = dynamic_cast<MidiIO *>(module);
		midiChoice->box.pos = Vec(margin, yPos);
		midiChoice->box.size.x = box.size.x - 10;
		addChild(midiChoice);
		yPos += midiChoice->box.size.y + margin * 6;
	}


	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Clock 1 Ratio";
		addChild(label);
		yPos += labelHeight + margin;

		ClockRatioChoice *ratioChoice = new ClockRatioChoice();
		ratioChoice->clockRatio = &module->clock1ratio;
		ratioChoice->box.pos = Vec(margin, yPos);
		ratioChoice->box.size.x = box.size.x - 10;
		addChild(ratioChoice);
		yPos += ratioChoice->box.size.y + margin + 5;

	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Clock 1 Pulse";
		addChild(label);

		addOutput(createOutput<PJ3410Port>(Vec(15 * 6, yPos - 5), module, MIDIClockToCVInterface::CLOCK1_PULSE));
		yPos += labelHeight + margin * 4;
	}


	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Clock 2 Ratio";
		addChild(label);
		yPos += labelHeight + margin;

		ClockRatioChoice *ratioChoice = new ClockRatioChoice();
		ratioChoice->clockRatio = &module->clock2ratio;
		ratioChoice->box.pos = Vec(margin, yPos);
		ratioChoice->box.size.x = box.size.x - 10;
		addChild(ratioChoice);
		yPos += ratioChoice->box.size.y + margin + 5;

	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Clock 2 Pulse";
		addChild(label);

		addOutput(createOutput<PJ3410Port>(Vec(15 * 6, yPos - 5), module, MIDIClockToCVInterface::CLOCK2_PULSE));
		yPos += labelHeight + margin * 7;
	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Clock Start";
		addChild(label);
		addOutput(createOutput<PJ3410Port>(Vec(15 * 6, yPos - 5), module, MIDIClockToCVInterface::CLOCK_START_PULSE));
		yPos += 40;
	}

	{
		Label *label = new Label();
		label->box.pos = Vec(margin, yPos);
		label->text = "Clock Stop";
		addChild(label);

		addOutput(createOutput<PJ3410Port>(Vec(15 * 6, yPos - 5), module, MIDIClockToCVInterface::CLOCK_STOP_PULSE));
	}
}

void MIDIClockToCVWidget::step() {

	ModuleWidget::step();
}