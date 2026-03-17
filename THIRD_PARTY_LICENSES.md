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

## PreenFM2 Synthesizer Engine

src/pfm/ contains source files adapted from the
[PreenFM2](https://github.com/Ixox/preenfm2) synthesizer engine by Xavier Hosxe,
used in the **Minimalith** module.

PreenFM2 is licensed under the **GNU General Public License v3.0 or later**
(GPL-3.0-or-later).  The full license text is available at:
<https://www.gnu.org/licenses/gpl-3.0.html>

Some PreenFM2 source files also carry copyright notices from Emilie Gillet
(Copyright 2009–2011) for portions originally derived from early Mutable
Instruments firmware, used here under GPL-3.0.

---

MorphWorx itself is licensed under the **GNU General Public License v3.0 or later**.
See plugin.json for the SPDX identifier, and
<https://www.gnu.org/licenses/gpl-3.0.html> for the full license text.
