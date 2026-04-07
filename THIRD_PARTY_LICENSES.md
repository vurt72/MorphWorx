# Third-Party Licenses

MorphWorx incorporates portions of code from the following open-source projects.
All original copyright notices and license texts are reproduced in full below
as required by the respective licenses.

---

## Mutable Instruments Plaits

Portions of src/Phaseon1.cpp adapt the **TIMBRE** and **COLOR** macro parameter
algorithms from [Mutable Instruments Plaits](https://github.com/pichenettes/eurorack)
by Emilie Gillet.  These concepts govern spectral character control (COLOR) and
wavetable / FM complexity control (TIMBRE) in the Phaseon1 module.

`
MIT License

Copyright (c) 2021 Emilie Gillet.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
`

---

## Mutable Instruments Braids and Ornament & Crime (Quantizer)

src/Quantizer.hpp adapts quantizer scale definitions from
[Mutable Instruments Braids](https://github.com/pichenettes/eurorack) and
[Ornament & Crime](https://github.com/mxmxmx/O_C).

- **Braids** — Copyright (c) 2012 Emilie Gillet.
- **Ornament & Crime** — Copyright (c) 2016 Patrick Dowling, Max Stadler, Tim Churches.

These files were originally made available under the MIT License
(same text as reproduced above, with the respective copyright holders).

---

## Mutable Instruments Elements (Ferroklast Reverb)

`src/dsp/FerroReverb.hpp` adapts the Griesinger/Dattorro reverb implementation
from **Mutable Instruments Elements** by Emilie Gillet for use in the
**Ferroklast** module.

This reverb code is used under the **MIT License** (same text as reproduced
above for other Mutable Instruments-derived components).

---

## Ornament & Crime Hemisphere TB-3PO (SlideWyrm)

`src/SlideWyrm.cpp` is a VCV Rack port of the **TB-3PO** Hemisphere applet for
**Ornament & Crime**.

- Original TB-3PO code — **Logarhythm**.
- Ornament & Crime platform — Copyright (c) 2016 Patrick Dowling,
  Max Stadler, Tim Churches.

This code is used under the **MIT License** (same text as reproduced above for
other Ornament & Crime-derived components).

---

## PreenFM2 Synthesizer Engine

src/pfm/ contains source files adapted from the
[PreenFM2](https://github.com/Ixox/preenfm2) synthesizer engine by Xavier Hosxe,
used in the **Minimalith** module.

**Minimalith** is a port and derivative adaptation of PreenFM2 rather than a
verbatim upstream copy. The project includes the adapted PreenFM2 engine in
`src/pfm/`, along with MorphWorx-specific integration and additional
functionality in `src/Minimalith.cpp` for Rack / MetaModule use.

PreenFM2 is licensed under the **GNU General Public License v3.0 or later**
(GPL-3.0-or-later).  The full license text is available at:
<https://www.gnu.org/licenses/gpl-3.0.html>

Some PreenFM2 source files also carry copyright notices from Emilie Gillet
(Copyright 2009–2011) for portions originally derived from early Mutable
Instruments firmware, used here under GPL-3.0.

---

## TR-909 Web Simulator Sample Assets

MorphWorx reuses TR-909 drum sample assets derived from
[tr-909-main](https://github.com/andremichelle/tr-909) by André Michelle.
The source assets are distributed in that project as `resources/ride.raw`,
`resources/bassdrum-attack.raw`, `resources/bassdrum-cycle.raw`,
`resources/clap.raw`, and `resources/rim.raw`.

In MorphWorx these are used by **Ferroklast**, including:

- `resources/ride.raw` for the Ferroklast **ride** voice.
- `resources/clap.raw` for the Ferroklast **clap** voice.
- `resources/bassdrum-attack.raw` and `resources/bassdrum-cycle.raw` for the
	Ferroklast **kick** voice.
- `resources/rim.raw` for the Ferroklast **rim** voice.

`
MIT License

Copyright (c) 2022 André Michelle

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
`

---

MorphWorx itself is licensed under the **GNU General Public License v3.0 or later**.
See plugin.json for the SPDX identifier, and
<https://www.gnu.org/licenses/gpl-3.0.html> for the full license text.
