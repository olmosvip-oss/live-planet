Build a website for **Live Planet**, an audio plugin (VST3 / AU / CLAP) for music producers. It's an instrument played by the real, live weather of any city on Earth: rain becomes melody, clouds make the chords bigger, temperature sets the mood, wind sets the movement. It always stays in key and in time with your song.

## Feel
Calm, premium, minimal and tactile, like a small boutique hardware maker. Lots of whitespace, few words, big product shots, gentle motion. Inspired by boutique plugin sites (one hero product, big product imagery, short punchy copy, sound demos you can play, one simple price card), but with its own identity. Do not copy any other company's text, layout, logo or branding.

## Visual identity
- Palette from nature: soft grass green (#5B8A2E, light #9FCB55, deep #27461A), pale sky (#EEF3F6 to #C8DBE8), birch bark off-white (#DCD9D2, light #EEECE7), ink (#2C2B27), muted text (#7A766C). Space black (#0A0A0F) only for "screen" moments.
- Font: Space Grotesk (light for big headings, medium for UI), lowercase headings, generous letter spacing on small labels.
- Shapes: soft rounded corners (16-24px), raised "bark slab" cards with soft shadows, pill buttons. Subtle grass texture or soft topiary blob shapes as decoration.
- Motion: slow fade/slide reveals on scroll, a gently floating product image, hover lift on cards. Respect prefers-reduced-motion.

## Pages
1. **Home** (/)
2. **Product** (/product): deeper feature tour
3. **Support** (/support): install steps, FAQ, contact
4. **About** (/about)

## Home page sections (in order)
1. **Slim announcement bar** (dismissible): "live planet 1.0 · out now · get it".
2. **Header**: small planet-ring logo + "liveplanet" wordmark left; links (product, sounds, support, about) center; "buy" pill button right. Sticky, frosted, collapses to a burger menu on mobile.
3. **Hero**: headline "an instrument played by the sky". Subline: "pick any city. its live weather plays dreamy chords and melodies, in key and in time with your song." Two buttons: "buy" and "try it free". Big product screenshot (I'll upload it) floating on a pale sky background with soft grass shapes.
4. **Live weather demo strip**: 3-4 city cards (tokyo · drizzle, reykjavik · snow, dubai · sunny, london · storm), each with a play button for an audio clip and a short line ("drizzle in tokyo plays a dusty rhodes"). Use placeholder audio players I can swap.
5. **How it works**: a row of 6 small tiles with simple line icons: rain → melody, clouds → bigger chords, temperature → mood, wind → movement, snow → slow and hazy, lightning → big hits.
6. **Feature blocks** (alternating image/text, short copy each):
   - "11 instruments": glass, choir, felt piano, music box, guitar, strings, vapor, rhodes, kalimba, vibes, ocarina, or drop in your own sample.
   - "auto mode: the weather picks the instrument".
   - "nostalgia": one knob for vinyl crackle, tape wobble and a warm, worn-out tone.
   - "drag the notes into your song": every note it plays can be dragged out as MIDI.
   - "your sound, your sample": automatic pitch detection, choose the part that plays, tune it.
   - "four seasons": the plugin itself changes colour (spring, moss, autumn, frost).
7. **Presets**: a horizontal scrolling row of preset name chips ("rainy window", "grandma's music box", "4am drive", "snow globe", "cassette diary", "vinyl sunset"...).
8. **Pricing card** (one centered card on a bark-colored slab): "full licence · one-time · yours forever · free updates", price placeholder ($XX) with an optional launch-discount line, a "buy now" button, and a secondary "try free demo" link. Under it: "add the sample pack" toggle as an optional add-on. Small text: "instant download · secure checkout".
9. **Formats & requirements** accordion: formats VST3, AU, CLAP + standalone app; macOS 11+ (Apple Silicon and Intel), Windows 10/11 64-bit; works in Ableton Live, Logic Pro, FL Studio, Cubase, Studio One, Bitwig, Reaper and any VST3/AU/CLAP host. Internet connection needed for live weather.
10. **Community / testimonials**: layout for quote cards and an artist row, but fill it with clearly marked placeholder content ("your quote here"). Do not invent real names, stream counts or endorsements.
11. **Newsletter**: "get new sounds and updates", email field + button (front-end only for now).
12. **Footer**: wordmark, links, small print ("© live planet audio"), privacy and terms links.

## Product page
Hero with the product shot, then a tour of each panel of the plugin (the planet screen, the instrument keys, the knobs grouped as mix / texture / space, the sample editor), each with a short caption. Include an audio "before / after" toggle component (dry vs. live planet) with placeholder audio. Repeat the pricing card and formats accordion at the bottom.

## Support page
Install steps for Mac and Windows (plugin folder paths), a note that Mac users may need to allow the plugin in Privacy & Security, an FAQ accordion (does it need internet? what if I'm offline? can I use my own samples? which DAWs?), and a contact email placeholder.

## Technical
- Responsive, mobile-first, fast. Semantic HTML, accessible contrast, visible focus states.
- Buy buttons link to a placeholder checkout URL I'll replace later (e.g. Lemon Squeezy or Gumroad).
- Keep all copy short, lowercase where it's a heading, friendly and calm.

I'll upload: the plugin screenshot(s), audio clips, and a logo. Use tasteful placeholders until then.
