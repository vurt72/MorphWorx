# Kinetrax Manual

Kinetrax is an eight-zone comparator network with two simultaneous views of motion: state outputs tell you which zone the signal currently occupies, and the transition output emits bursts when the signal crosses zone boundaries.

This version also adds a secondary **Span 2** input that can bend the boundary behavior of the primary Span gesture for more rhythmic variation.

## Controls

- **SPAN** adds manual offset to the internal channel-index signal. With no cable patched, it can manually scan the eight channels.
- **ATTN** is a bipolar attenuverter for the incoming Span CV, matching the GTE-style patching model more closely.
- **SPACE** sets the total voltage range required for Span to traverse all eight channels. The control is applied nonlinearly, so lower values make Kinetrax much more reactive while the top of the range gives coarser, broader movement.
- **WARP** sets how strongly **Span 2** perturbs channel-boundary crossings.
- **CV MODE** selects the derived CV output: stepped channel index, slewed channel index, or transition-density envelope.

## Inputs

- **SPAN IN** is the tracked signal.
- **SPAN 2** is a secondary modulation input that perturbs boundary placement instead of directly replacing the main channel trajectory.
- **SPAN CV** modulates the manual Span offset directly.
- **ATTN CV** modulates the Span attenuverter amount directly.
- **WARP CV** modulates the Warp amount directly.
- **CLOCK** enables sample-and-hold updates. When patched, Kinetrax only updates the active zone on rising clock edges.

## Outputs

- **1-8** are mutually exclusive zone gates. Exactly one zone output is high for each poly channel.
- **ODD / EVEN** are derived state gates from the current zone.
- **TRANS** emits a pulse when a boundary is crossed. If one update jumps across multiple zones, Kinetrax emits a short burst with one pulse per crossed boundary.
- **CV** is a derived control-voltage output whose behavior depends on the selected mode.

## CV Modes

- **INDEX** outputs the current channel position as a stepped control voltage.
- **SLEW** outputs a smoothed version of the channel index for gentler filter or timbre modulation, with a roughly 30 ms time constant.
- **FLUX** outputs a decaying envelope that rises by about +2.5 V per transition event and then falls back with a roughly 280 ms time constant between bursts.

## Behavior Notes

- The active channel range starts from the Span low end and expands upward according to **SPACE**, which is closer to the documented GTE behavior than the previous centered mapping.
- Internally, **SPACE** is squared before it scales the tracked voltage range. That gives finer control in the reactive low end and broader travel near the top of the knob.
- With **SPAN** fully counterclockwise, **ATTN** fully clockwise, and **SPACE** fully clockwise, Kinetrax is shaped to behave more like a channel-index translator.
- **Span 2** acts as a warp source. It has the strongest effect near channel boundaries and less effect in the middle of a channel, so the main Span gesture remains readable while rhythmic crossings become more varied.
- The warped result affects the full final state engine, so **Zone 1-8**, **Odd/Even**, and **TRANS** all respond to the interaction between **Span** and **Span 2**.
- The three-position **CV MODE** switch selects whether the derived CV follows absolute channel position, a slewed position, or transition density.
- A small internal hysteresis band reduces chatter at zone boundaries.
- In clocked mode, large movements between clocks are preserved and turned into multi-pulse bursts on the next rising edge.
- Transition pulses are about 1 ms wide, while serialized channel stepping uses about 5 ms holds to approximate the documented FDD behavior.

## Polyphony

Kinetrax tracks each poly channel independently. The zone outputs, odd/even outputs, and transition pulses all follow the channel count of the widest connected input.