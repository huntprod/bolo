;(function (document,window,undefined) {

// convert a color name / hex string into {r:R,g:G,b:B}
var w3c_named = {
	"aliceblue":             "#f0f8ff",
	"antiquewhite":          "#faebd7",
	"aqua":                  "#00ffff",
	"aquamarine":            "#7fffd4",
	"azure":                 "#f0ffff",
	"beige":                 "#f5f5dc",
	"bisque":                "#ffe4c4",
	"black":                 "#000000",
	"blanchedalmond":        "#ffebcd",
	"blue":                  "#0000ff",
	"blueviolet":            "#8a2be2",
	"brown":                 "#a52a2a",
	"burlywood":             "#deb887",
	"cadetblue":             "#5f9ea0",
	"chartreuse":            "#7fff00",
	"chocolate":             "#d2691e",
	"coral":                 "#ff7f50",
	"cornflowerblue":        "#6495ed",
	"cornsilk":              "#fff8dc",
	"crimson":               "#dc143c",
	"cyan":                  "#00ffff",
	"darkblue":              "#00008b",
	"darkcyan":              "#008b8b",
	"darkgoldenrod":         "#b8860b",
	"darkgray":              "#a9a9a9",
	"darkgreen":             "#006400",
	"darkkhaki":             "#bdb76b",
	"darkmagenta":           "#8b008b",
	"darkolivegreen":        "#556b2f",
	"darkorange":            "#ff8c00",
	"darkorchid":            "#9932cc",
	"darkred":               "#8b0000",
	"darksalmon":            "#e9967a",
	"darkseagreen":          "#8fbc8f",
	"darkslateblue":         "#483d8b",
	"darkslategray":         "#2f4f4f",
	"darkturquoise":         "#00ced1",
	"darkviolet":            "#9400d3",
	"deeppink":              "#ff1493",
	"deepskyblue":           "#00bfff",
	"dimgray":               "#696969",
	"dodgerblue":            "#1e90ff",
	"firebrick":             "#b22222",
	"floralwhite":           "#fffaf0",
	"forestgreen":           "#228b22",
	"fuchsia":               "#ff00ff",
	"gainsboro":             "#dcdcdc",
	"ghostwhite":            "#f8f8ff",
	"gold":                  "#ffd700",
	"goldenrod":             "#daa520",
	"gray":                  "#808080",
	"green":                 "#008000",
	"greenyellow":           "#adff2f",
	"honeydew":              "#f0fff0",
	"hotpink":               "#ff69b4",
	"indianred":             "#cd5c5c",
	"indigo":                "#4b0082",
	"ivory":                 "#fffff0",
	"khaki":                 "#f0e68c",
	"lavender":              "#e6e6fa",
	"lavenderblush":         "#fff0f5",
	"lawngreen":             "#7cfc00",
	"lemonchiffon":          "#fffacd",
	"lightblue":             "#add8e6",
	"lightcoral":            "#f08080",
	"lightcyan":             "#e0ffff",
	"lightgoldenrodyellow":  "#fafad2",
	"lightgrey":             "#d3d3d3",
	"lightgreen":            "#90ee90",
	"lightpink":             "#ffb6c1",
	"lightsalmon":           "#ffa07a",
	"lightseagreen":         "#20b2aa",
	"lightskyblue":          "#87cefa",
	"lightslategray":        "#778899",
	"lightsteelblue":        "#b0c4de",
	"lightyellow":           "#ffffe0",
	"lime":                  "#00ff00",
	"limegreen":             "#32cd32",
	"linen":                 "#faf0e6",
	"magenta":               "#ff00ff",
	"maroon":                "#800000",
	"mediumaquamarine":      "#66cdaa",
	"mediumblue":            "#0000cd",
	"mediumorchid":          "#ba55d3",
	"mediumpurple":          "#9370d8",
	"mediumseagreen":        "#3cb371",
	"mediumslateblue":       "#7b68ee",
	"mediumspringgreen":     "#00fa9a",
	"mediumturquoise":       "#48d1cc",
	"mediumvioletred":       "#c71585",
	"midnightblue":          "#191970",
	"mintcream":             "#f5fffa",
	"mistyrose":             "#ffe4e1",
	"moccasin":              "#ffe4b5",
	"navajowhite":           "#ffdead",
	"navy":                  "#000080",
	"oldlace":               "#fdf5e6",
	"olive":                 "#808000",
	"olivedrab":             "#6b8e23",
	"orange":                "#ffa500",
	"orangered":             "#ff4500",
	"orchid":                "#da70d6",
	"palegoldenrod":         "#eee8aa",
	"palegreen":             "#98fb98",
	"paleturquoise":         "#afeeee",
	"palevioletred":         "#d87093",
	"papayawhip":            "#ffefd5",
	"peachpuff":             "#ffdab9",
	"peru":                  "#cd853f",
	"pink":                  "#ffc0cb",
	"plum":                  "#dda0dd",
	"powderblue":            "#b0e0e6",
	"purple":                "#800080",
	"rebeccapurple":         "#663399",
	"red":                   "#ff0000",
	"rosybrown":             "#bc8f8f",
	"royalblue":             "#4169e1",
	"saddlebrown":           "#8b4513",
	"salmon":                "#fa8072",
	"sandybrown":            "#f4a460",
	"seagreen":              "#2e8b57",
	"seashell":              "#fff5ee",
	"sienna":                "#a0522d",
	"silver":                "#c0c0c0",
	"skyblue":               "#87ceeb",
	"slateblue":             "#6a5acd",
	"slategray":             "#708090",
	"snow":                  "#fffafa",
	"springgreen":           "#00ff7f",
	"steelblue":             "#4682b4",
	"tan":                   "#d2b48c",
	"teal":                  "#008080",
	"thistle":               "#d8bfd8",
	"tomato":                "#ff6347",
	"turquoise":             "#40e0d0",
	"violet":                "#ee82ee",
	"wheat":                 "#f5deb3",
	"white":                 "#ffffff",
	"whitesmoke":            "#f5f5f5",
	"yellow":                "#ffff00",
	"yellowgreen":           "#9acd32"
}


window.color = {
	// Convert a color name (W3c) or hexadecimal literal (#rrggbb)
	// into an {r:RR, g:GG, b:BB} object.
	//
	// Example: color.rgb('pink') // -> {r:0xff, g:0xc0, b: 0xcb}
	//
	rgb: function (s) {
		if (s in w3c_named) { s = w3c_named[s]; }

		if (s.substr(0,1) != '#') {
			return {r:0,g:0,b:0}; // default to black
		}

		s = s.substr(1);
		if (s.length == 6) {
			return {
				r: parseInt("0x"+s.substr(0,2)),
				g: parseInt("0x"+s.substr(2,2)),
				b: parseInt("0x"+s.substr(4,2)),
			};
		}
		if (s.length == 3) {
			var dd = function (v) { return (v << 4) | v }
			return {
				r: dd(parseInt("0x"+s.substr(0,1))),
				g: dd(parseInt("0x"+s.substr(1,1))),
				b: dd(parseInt("0x"+s.substr(2,1))),
			}
		}
		throw(s);
	},

	// Convert an {r:RR, g:GG, b:BB} object into hex notation
	hex: function(c) {
		var hex = '0123456789abcdef';
		var x = function(v) { return hex[(v >> 4)] + hex[(v & 0xf)]; }
		return '#'+x(c.r)+x(c.g)+x(c.b);
	},


	// Convert an {r:R,g:G,b:B} value into {h:H,s:S,v:V}
	rgb2hsv: function(c) {
		var r = c.r / 256.0;
		var g = c.g / 256.0;
		var b = c.b / 256.0;

		var hsv = function(h,s,v) {
			return {
				h: parseInt(h * 255 + 0.5),
				s: parseInt(s * 255 + 0.5),
				v: parseInt(v * 255 + 0.5)
			};
		}

		var min = Math.min(r, g, b);
		var max = Math.max(r, g, b);

		var h, s, v = max;
		var delta = max - min;

		/* calculate saturation */
		if (max != 0) {
			s = delta / max;
		} else {
			// unsaturated colors are every hue and no hue, all at once
			return hsv(0,0,v);
		}

		if (r == max) {
			h = (g - b) / delta; // between yellow and magenta
		} else if (g == max) {
			h = 2 + (b - r) / delta; // between cyan and yellow
		} else {
			h = 4 + (r - g) / delta; // between magenta and cyan
		}

		h = h * 60 % 360; /* get out of polar */
		if (h < 0) { h += 360; }

		return hsv(h / 360.0, s,v);
	},

	// Convert an {h:H,s:S,v:V} value into {r:R,g:G,b:B}
	hsv2rgb: function(c) {
		var h = c.h / 256.0 * 360.0;
		var s = c.s / 256.0;
		var v = c.v / 256.0;

		var C = v * s;

		var h_ = h / 60;
		var x = C * (1 - Math.abs(h_ % 2 - 1));
		var r_, g_, b_;
		     if (h_ < 1) { r_ = C; g_ = x; b_ = 0; } // C,X,0
		else if (h_ < 2) { r_ = x, g_ = C; b_ = 0; } // X,C,0
		else if (h_ < 3) { r_ = 0, g_ = C, b_ = x; } // 0,C,X
		else if (h_ < 4) { r_ = 0, g_ = x, b_ = C; } // 0,X,C
		else if (h_ < 5) { r_ = x, g_ = 0; b_ = C; } // X,0,C
		else if (h_ < 6) { r_ = C, g_ = 0, b_ = x; } // C,0,X
		else             { r_ = 0; g_ = 0; b_ = 0; } // H is 'undefined'

		var m = v - C;
		return {
			r: parseInt((r_ + m) * 255.0 + 0.5),
			g: parseInt((g_ + m) * 255.0 + 0.5),
			b: parseInt((b_ + m) * 255.0 + 0.5),
		};
	},

	darken: function (name, factor) {
		if (typeof(factor) === 'undefined') {
			factor = 0.66;
		}
		if (factor <= 0 || factor >= 1.0) {
			throw 'color.darken() factor '+factor.toString()+' is not within the range (0,1)';
		}

		var hsv = color.rgb2hsv(color.rgb(name));
		hsv.v *= factor;
		return color.hex(color.hsv2rgb(hsv));
	},

	lighten: function (name, factor) {
		if (typeof(factor) === 'undefined') {
			factor = 0.66;
		}
		if (factor <= 0 || factor >= 1.0) {
			throw 'color.lighten() factor '+factor.toString()+' is not within the range (0,1)';
		}

		var hsv = color.rgb2hsv(color.rgb(name));
		hsv.v /= factor;
		return color.hex(color.hsv2rgb(hsv));
	},

	// parse color specification (#fg/#bg) into a {fg: #fg, bg: #bg} object.
	spec: function (s) {
		var c = (s || '').split('/');
		if (c.length == 2) {
			return { fg: c[0], bg: c[1] };
		}
		return { fg: '#fff', bg: c[0] };
	},
};

})(document,window);
