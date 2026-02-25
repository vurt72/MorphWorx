/*
 * Copyright 2013 Xavier Hosxe
 *
 * Author: Xavier Hosxe (xavier . hosxe (at) gmail . com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LFOOSC_H_
#define LFOOSC_H_

#include "Lfo.h"
#include "Osc.h"



class LfoOsc: public Lfo {
public:
    virtual ~LfoOsc() {};

	void init(struct LfoParams *lfoParams, float* lfoPhase, Matrix* matrix, SourceEnum source, DestinationEnum dest);

	void valueChanged(int encoder) {
	    switch (encoder) {
	    case ENCODER_LFO_KSYNC:
            this->rampInv = 50 * invTab[(int)(lfo->keybRamp * 50.0f)];
            this->ramp = lfo->keybRamp;
            if (this->ramp < 0 ) {
                // resync all LFO
                phase = 0;
            }
            break;
	    case ENCODER_LFO_FREQ:
	        isNotMidiSynchronized = ((lfo->freq * 10.0f) < LFO_MIDICLOCK_MC_DIV_16);
	        break;
	    }
	}


	void midiClock(int songPosition, bool computeStep);

	void nextValueInMatrix();

	void noteOn();

	void noteOff() {
		// Nothing to do
	}



private:
	LfoType type = LFO_SIN;
	LfoParams* lfo = nullptr;
    float currentRamp = 0.0f, ramp = 0.0f, rampInv = 0.0f;
    float phase = 0.0f;
    float* initPhase = nullptr;
    DestinationEnum destination = (DestinationEnum)0;
    float currentRandomValue = 0.0f;
    float currentFreq = 0.0f;
    //
    bool isNotMidiSynchronized = true;

};

#endif /* LFOOSC_H_ */
