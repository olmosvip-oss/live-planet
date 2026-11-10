#pragma once

// The living planet, drawn on the graphics card. Everything here is procedural:
// space, nebula, the planet, its clouds and lights, and the notes as light.
namespace lp::shader
{

static const char* vertex = R"(
attribute vec2 position;
void main()
{
    gl_Position = vec4 (position, 0.0, 1.0);
}
)";

static const char* fragment = R"(
uniform vec2  uRes;       // framebuffer size in pixels
uniform float uTime;
uniform float uScale;     // pixels per UI point
uniform vec4  uPlanet;    // centre x, y (GL pixels), radius (pixels), unused
uniform vec4  uW1;        // warmth, rain, snow, wind      (0..1)
uniform vec4  uW2;        // cloud, daylight, flash, tension
uniform vec4  uW3;        // fog, aurora, wind direction (rad), city latitude (rad)
uniform float uCityLon;
uniform vec3  uPalA;
uniform vec3  uPalB;
uniform vec3  uPalC;
uniform vec4  uNotes[24]; // hue, kind (0 chord, 1 melody, 2 bass), age 0..1, seed
uniform float uNoteCount;
uniform vec4  uPanels[3]; // centre x, y, half width, half height (GL pixels)
uniform float uPanelRadius;
uniform vec2  uOrigin;    // bottom-left of the screen window, in pixels

vec2 FRAG;                // pixel position inside the screen

float hash (vec3 p)
{
    p = fract (p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract (p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise (vec3 x)
{
    vec3 i = floor (x);
    vec3 f = fract (x);
    f = f * f * (3.0 - 2.0 * f);
    return mix (mix (mix (hash (i + vec3 (0.0, 0.0, 0.0)), hash (i + vec3 (1.0, 0.0, 0.0)), f.x),
                     mix (hash (i + vec3 (0.0, 1.0, 0.0)), hash (i + vec3 (1.0, 1.0, 0.0)), f.x), f.y),
                mix (mix (hash (i + vec3 (0.0, 0.0, 1.0)), hash (i + vec3 (1.0, 0.0, 1.0)), f.x),
                     mix (hash (i + vec3 (0.0, 1.0, 1.0)), hash (i + vec3 (1.0, 1.0, 1.0)), f.x), f.y), f.z);
}

float fbm (vec3 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 5; i++)
    {
        s += a * noise (p);
        p = p * 2.03 + vec3 (1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s;
}

float fbm3 (vec3 p)
{
    float a = 0.5, s = 0.0;
    for (int i = 0; i < 3; i++)
    {
        s += a * noise (p);
        p = p * 2.03 + vec3 (1.7, 9.2, 3.1);
        a *= 0.5;
    }
    return s / 0.875;
}

vec3 hsv (float h, float s, float v)
{
    vec3 k = clamp (abs (mod (h * 6.0 + vec3 (0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return v * mix (vec3 (1.0), k, s);
}

vec3 rotY (vec3 v, float a) { float c = cos (a), s = sin (a); return vec3 (c * v.x + s * v.z, v.y, -s * v.x + c * v.z); }
vec3 rotX (vec3 v, float a) { float c = cos (a), s = sin (a); return vec3 (v.x, c * v.y - s * v.z, s * v.y + c * v.z); }

float segDist (vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp (dot (pa, ba) / dot (ba, ba), 0.0, 1.0);
    return length (pa - ba * h);
}

float sdRoundBox (vec2 p, vec2 b, float r)
{
    vec2 q = abs (p) - b + r;
    return length (max (q, 0.0)) + min (max (q.x, q.y), 0.0) - r;
}

// deep space: nebula clouds in the weather's colours, stars, rain and snow
vec3 space (vec2 uv, bool detail)
{
    float t = uTime * 0.012;
    float n1 = fbm3 (vec3 (uv * 1.1, t));
    float n2 = fbm3 (vec3 (uv * 2.3 + vec2 (5.2, 1.3), t * 1.7 + 3.0));
    float n3 = fbm3 (vec3 (uv * 0.7 - vec2 (2.0, 4.0), t * 0.6 + 9.0));
    vec3 col = vec3 (0.012, 0.012, 0.03);
    col += uPalB * 0.22 * smoothstep (0.35, 0.85, n1);
    col += uPalA * 0.20 * pow (smoothstep (0.4, 0.9, n2), 1.5);
    col += uPalC * 0.10 * smoothstep (0.5, 0.9, n3);
    col *= 0.6 + 0.3 * (1.0 - uW2.y * 0.3);

    if (! detail)
        return col;

    // stars
    vec2 cell = FRAG / (uScale * 9.0);
    vec2 id = floor (cell);
    vec2 f = fract (cell) - 0.5;
    float h = hash (vec3 (id, 3.1));
    if (h > 0.9)
    {
        vec2 o = vec2 (hash (vec3 (id, 7.0)), hash (vec3 (id, 9.0))) - 0.5;
        float d = length (f - o * 0.6);
        float tw = 0.55 + 0.45 * sin (uTime * (1.0 + h * 3.0) + h * 40.0);
        float b = (1.0 - uW2.x * 0.6) * (1.0 - uW2.y * 0.45);
        col += mix (vec3 (0.8, 0.9, 1.0), uPalC, h - 0.9) * smoothstep (0.09, 0.0, d) * tw * b * 1.4;
    }

    vec2 px = FRAG / uScale;

    // rain: thin falling streaks, leaning with the wind
    if (uW1.y > 0.04)
    {
        float lean = 0.15 + 0.5 * uW1.w;
        vec2 q = vec2 (px.x + px.y * lean, px.y);
        float column = floor (q.x / 7.0);
        float hc = hash (vec3 (column, 0.0, 4.0));
        float speed = 520.0 + hc * 380.0;
        float y = fract ((q.y + uTime * speed + hc * 977.0) / 260.0);
        float on = step (1.0 - uW1.y * 0.55, hash (vec3 (column, floor ((q.y + uTime * speed + hc * 977.0) / 260.0), 2.0)));
        float x = abs (fract (q.x / 7.0) - 0.5);
        col += mix (uPalC, vec3 (1.0), 0.4) * on * smoothstep (0.12, 0.0, x) * smoothstep (0.22, 0.0, y) * 0.5;
    }

    // snow: soft drifting flakes
    if (uW1.z > 0.01)
    {
        for (int layer = 0; layer < 2; layer++)
        {
            float sz = layer == 0 ? 34.0 : 22.0;
            vec2 q = px / sz + vec2 (sin (uTime * 0.3 + px.y * 0.01) * 0.3, uTime * (layer == 0 ? 1.1 : 0.7));
            vec2 sid = floor (q);
            vec2 sf = fract (q) - 0.5;
            float hs = hash (vec3 (sid, 11.0 + float (layer)));
            if (hs < uW1.z * 0.7)
            {
                vec2 o = vec2 (hash (vec3 (sid, 5.0)), hash (vec3 (sid, 6.0))) - 0.5;
                col += vec3 (0.9, 0.95, 1.0) * smoothstep (0.09, 0.0, length (sf - o * 0.6)) * 0.7;
            }
        }
    }
    return col;
}

void main()
{
    FRAG = gl_FragCoord.xy - uOrigin;
    vec2 frag = FRAG;
    vec2 uv = (frag - 0.5 * uRes) / uRes.y;
    vec3 col = space (uv, true);

    float R = uPlanet.z;
    vec2 p = (frag - uPlanet.xy) / R;   // planet units, y up
    float r = length (p);

    float lat = uW3.w, lon = uCityLon;
    float viewLon = lon + 0.35 * sin (uTime * 0.035);
    float viewLat = lat - 0.32;

    float warmth = uW1.x, rain = uW1.y, snow = uW1.z, wind = uW1.w;
    float cloud = uW2.x, day = uW2.y, flash = uW2.z, tension = uW2.w;
    vec3 sun = normalize (mix (vec3 (-0.95, 0.35, -0.55), vec3 (-0.55, 0.45, 0.72), day));

    // outer glow of the atmosphere
    float bassPulse = 0.0;
    for (int i = 0; i < 24; i++)
    {
        if (float (i) >= uNoteCount) break;
        if (uNotes[i].y > 1.5) bassPulse += (1.0 - uNotes[i].z) * (1.0 - uNotes[i].z);
    }
    float glowAmt = 0.9 + 0.5 * min (bassPulse, 1.5) + flash * 1.5;
    {
        float d = max (r - 1.0, 0.0);
        float lit = 0.55 + 0.45 * max (dot (normalize (vec3 (p, 0.0)), sun), 0.0);
        col += uPalA * (exp (-d * 9.0) * 0.85 + exp (-d * 2.2) * 0.22) * glowAmt * lit;
        col += uPalB * exp (-d * 3.5) * 0.12 * glowAmt;
    }

    // aurora ribbons over the pole when it's cold
    if (uW3.y > 0.01)
    {
        float ang = atan (p.y, p.x);
        float wave = 0.05 * sin (ang * 7.0 + uTime * 0.6) + 0.03 * sin (ang * 13.0 - uTime * 0.9);
        float band = exp (-pow ((r - 1.12 - wave) * 11.0, 2.0)) * smoothstep (0.15, 0.85, p.y / max (r, 0.001));
        float curtain = 0.6 + 0.4 * sin (ang * 40.0 + uTime * 2.0 + sin (ang * 9.0) * 3.0);
        vec3 ac = mix (vec3 (0.2, 1.0, 0.65), vec3 (0.75, 0.35, 1.0), 0.5 + 0.5 * sin (ang * 2.0 + uTime * 0.25));
        if (r > 0.97 || p.y > 0.0)
            col += ac * band * curtain * uW3.y * 0.9;
    }

    // the planet itself
    if (r < 1.0)
    {
        vec3 n = vec3 (p, sqrt (1.0 - r * r));
        vec3 s = rotY (rotX (n, -viewLat), viewLon);   // point on the planet

        float land = fbm (s * 1.7 + vec3 (3.1, 0.4, 7.7));
        float isLand = smoothstep (0.5, 0.53, land);
        vec3 ocean = mix (vec3 (0.01, 0.025, 0.08) + uPalB * 0.1, uPalA * 0.55 + vec3 (0.0, 0.04, 0.08), smoothstep (0.3, 0.5, land));
        vec3 ground = mix (mix (uPalC, uPalA, 0.3) * 0.42, mix (uPalB, uPalC, 0.35) * 0.55, smoothstep (0.53, 0.75, land));
        ground *= 0.85 + 0.3 * fbm3 (s * 9.0);
        vec3 surf = mix (ocean, ground, isLand);
        float iceLine = 0.92 - (1.0 - warmth) * 0.32 - snow * 0.2;
        float ice = smoothstep (iceLine, iceLine + 0.05, abs (s.y) + (fbm3 (s * 5.0) - 0.5) * 0.25);
        surf = mix (surf, vec3 (0.88, 0.93, 1.0), max (ice, isLand * snow * 0.8));

        float ndl = dot (n, sun);
        float light = smoothstep (-0.12, 0.55, ndl);

        // clouds, drifting with the wind, swirling more when the sky is tense
        vec3 cs = rotY (s, uTime * (0.006 + 0.03 * wind));
        float swirl = tension * 0.6;
        cs += swirl * vec3 (sin (cs.y * 4.0 + uTime * 0.1), 0.0, cos (cs.y * 4.0 + uTime * 0.1)) * 0.15;
        float cn = fbm (cs * 2.6 + vec3 (0.0, uTime * 0.01, 0.0));
        float cover = mix (0.68, 0.36, cloud) - rain * 0.05;
        float clouds = smoothstep (cover, cover + 0.17, cn);
        float cloudShadow = smoothstep (cover, cover + 0.2, fbm (rotY (cs, 0.04) * 2.6));

        vec3 colP = surf * (0.05 + 1.15 * light) * (1.0 - cloudShadow * 0.35);

        // sunlight glinting off the sea
        vec3 refl = reflect (-sun, n);
        colP += mix (vec3 (1.0, 0.95, 0.85), uPalC, 0.3) * pow (max (refl.z, 0.0), 30.0) * (1.0 - isLand) * light * 0.7 * (1.0 - clouds);

        // city lights on the night side
        float cityN = smoothstep (0.8, 0.9, noise (s * 95.0)) * smoothstep (0.5, 0.68, fbm3 (s * 6.0));
        float nightSide = smoothstep (0.25, 0.0, ndl);
        colP += vec3 (1.0, 0.74, 0.42) * cityN * isLand * nightSide * (1.0 - clouds) * 1.3;

        vec3 cloudCol = mix (vec3 (0.42, 0.45, 0.6), vec3 (1.0), 0.35 + 0.65 * (1.0 - tension * 0.5));
        cloudCol = mix (cloudCol, uPalC, 0.12);
        colP = mix (colP, cloudCol * (0.06 + 1.05 * light) + uPalB * flash * 0.8, clouds * 0.92);

        // atmosphere: coloured rim and a soft haze
        float fres = pow (1.0 - n.z, 2.2);
        colP += uPalA * fres * (0.25 + 0.9 * light) * 1.1 * glowAmt;
        colP = mix (colP, uPalA * 0.6 + vec3 (0.1), uW3.x * 0.35 * (1.0 - n.z * 0.5));

        // the city: a pulsing beacon
        vec3 cityW = vec3 (cos (lat) * sin (lon), sin (lat), cos (lat) * cos (lon));
        vec3 cv = rotX (rotY (cityW, -viewLon), viewLat);
        if (cv.z > 0.0)
        {
            float d = length (p - cv.xy);
            float pulse = fract (uTime * 0.45);
            colP += mix (uPalC, vec3 (1.0), 0.5) * (smoothstep (0.028, 0.0, d) * 1.6 + exp (-d * 22.0) * 0.5);
            colP += uPalC * smoothstep (0.012, 0.0, abs (d - pulse * 0.22)) * (1.0 - pulse) * 1.2;
        }

        float edge = smoothstep (1.0, 0.993, r);
        col = mix (col, colP, edge);
    }

    // notes as light
    for (int i = 0; i < 24; i++)
    {
        if (float (i) >= uNoteCount) break;
        vec4 nt = uNotes[i];
        vec3 nc = hsv (nt.x, 0.62, 1.0);
        float age = nt.z;
        float seed = nt.w;

        if (nt.y < 0.5)
        {
            // chord: a glowing ring orbiting the planet
            float th = seed * 6.2831;
            vec2 q = vec2 (cos (th) * p.x + sin (th) * p.y, -sin (th) * p.x + cos (th) * p.y) * vec2 (1.0, 1.0);
            float tilt = 0.22 + 0.12 * fract (seed * 3.7);
            vec2 e = vec2 (q.x, q.y / tilt);
            float rr = 1.22 + 0.32 * fract (seed * 7.3);
            float d = abs (length (e) - rr) * tilt;
            bool behind = r < 1.0 && e.y > 0.0;
            if (! behind)
            {
                float life = pow (1.0 - age, 1.4) * smoothstep (0.0, 0.08, age);
                col += nc * (exp (-d * d * 2600.0) * 0.85 + exp (-d * 38.0) * 0.22) * life;
                // a bright spark running round the ring
                float a = uTime * (0.5 + fract (seed * 5.1)) + seed * 20.0;
                vec2 sp = vec2 (cos (a) * rr, sin (a) * rr * tilt);
                sp = vec2 (cos (th) * sp.x - sin (th) * sp.y, sin (th) * sp.x + cos (th) * sp.y);
                if (! (sin (a) > 0.0 && length (sp) < 1.0))
                    col += mix (nc, vec3 (1.0), 0.5) * exp (-length (p - sp) * 55.0) * life * 1.4;
            }
        }
        else if (nt.y < 1.5)
        {
            // melody: a shooting star that splashes onto the planet
            float ang = 0.35 + seed * 2.4;          // arrives from above
            vec2 dir = vec2 (cos (ang), sin (ang));
            float tt = clamp (age / 0.22, 0.0, 1.0);
            float dist = mix (2.6, 1.0, tt * tt);
            vec2 head = dir * dist;
            vec2 tail = dir * (dist + 0.55 * (1.0 - tt * 0.6));
            if (age < 0.22)
            {
                float d = segDist (p, head, tail);
                float along = clamp (dot (p - head, tail - head) / dot (tail - head, tail - head), 0.0, 1.0);
                col += nc * exp (-d * 140.0) * (1.0 - along) * 1.2;
                col += mix (nc, vec3 (1.0), 0.6) * exp (-length (p - head) * 60.0) * 1.5;
            }
            else
            {
                float k = (age - 0.22) / 0.78;
                vec2 hit = dir * 1.0;
                vec2 rel = p - hit;
                rel = vec2 (dot (rel, vec2 (-dir.y, dir.x)), dot (rel, dir) * 2.2);
                float d = abs (length (rel) - k * 0.2);
                col += nc * exp (-d * 150.0) * pow (1.0 - k, 2.0) * 1.0;
                col += mix (nc, vec3 (1.0), 0.5) * exp (-length (p - hit) * 30.0) * pow (1.0 - k, 4.0) * 1.5;
            }
        }
        else
        {
            // bass: a shockwave rolling out from the planet
            float rad = 1.0 + age * 1.1;
            float d = abs (r - rad);
            col += nc * exp (-d * 28.0) * pow (1.0 - age, 2.5) * 0.45;
        }
    }

    // lightning flash
    col += vec3 (0.75, 0.7, 1.0) * flash * 0.18;

    // frosted glass panels
    for (int i = 0; i < 3; i++)
    {
        vec4 pr = uPanels[i];
        if (pr.z <= 0.0) continue;
        float d = sdRoundBox (frag - pr.xy, pr.zw, uPanelRadius);
        if (d < 0.0)
        {
            vec3 frost = space (uv, false) * 1.1 + vec3 (0.028, 0.03, 0.055);
            col = mix (col, frost, 0.78);
            float topY = (frag.y - (pr.y - pr.w)) / (2.0 * pr.w);
            col += vec3 (1.0) * 0.035 * smoothstep (0.55, 1.0, topY);
            col += uPalA * 0.03 * smoothstep (0.6, 0.0, topY);
        }
        // soft coloured shadow/glow just outside the edge
        col += uPalA * 0.05 * exp (-max (d, 0.0) / (10.0 * uScale)) * step (0.0, d);
    }

    // finish: gentle tone curve, vignette and film grain
    col *= 1.0 - 0.35 * pow (length (uv * vec2 (0.8, 1.0)), 2.0);
    col = 1.0 - exp (-col * 1.35);
    col += (hash (vec3 (frag, fract (uTime) * 100.0)) - 0.5) * 0.018;
    gl_FragColor = vec4 (col, 1.0);
}
)";

} // namespace lp::shader
