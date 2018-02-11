QUnit.module("BoardCode");
var logger = {
  saved: window.console.log,
  start: function () {
    logger.reset();
    window.console.log = function (m) {
      window.console.messages.push(m);
    };
  },
  stop: function () {
    window.console.log = logger.saved;
  },
  reset: function () {
    window.console.messages = [];
  },
  buffer: function () {
    return window.console.messages.join("");
  }
};
QUnit.test("log-based validation", function (Ω) {
  logger.start();
  $('.log-test').each(function (i, board) {
    try {
      logger.reset();
      new Board($(board).text());
      Ω.equal(logger.buffer(),
              $(board).attr('should-log'),
              $(board).attr('assertion'));
    } catch (e) {
      Ω.ok(false, $(board).attr('assertion') + ": threw '"+e.toString()+"'");
    }
  });
  logger.stop();
});


QUnit.test("valid boards", function (Ω) {
  $('.valid-board').each(function (i, board) {
    try {
      new Board($(board).text());
      Ω.ok(true, $(board).attr('assertion'));
    } catch (e) {
      Ω.ok(false, $(board).attr('assertion') + ": threw '"+e.toString()+"'");
    }
  });
});


QUnit.test("static error detection", function (Ω) {
  Ω.throws(function () {
    new Board('let s := "unterminated string');
  },
  /unterminated string literal/i,
  "an unterminated double-quoted string is a static error");

  Ω.throws(function () {
    new Board('let s := [unterminated string');
  },
  /unterminated bracketed string literal/i,
  "an unterminated bracket-quoted string is a static error");

  Ω.throws(function () {
    new Board('log "${bad var}"');
  },
  /malformed variable name/i,
  "a malformed (bracketed) variable reference is a parse error");

  Ω.throws(function () {
    new Board('log "${unterminated"');
  },
  /unterminated bracketed variable/i,
  "an unterminated bracketed variable reference is a parse error");

  Ω.throws(function () {
    new Board($('#unknown-block-1').text());
  }, "the parser should catch unknown blocks");
});


QUnit.test("runtime error detection", function (Ω) {
  Ω.throws(function () {
    new Board('log "unknown: $var"');
  },
  /unable to find the variable/,
  "dereferencing an unbound variable is a semantic error");

  Ω.throws(function () {
    new Board('$undef := 123');
  },
  /undefined variable/i,
  "attempting to set a var that has not been let-bound should fail");
});



QUnit.test("imports", function (Ω) {
  var importer = function (name) {
    var lib = $('.import[name="'+name+'"]');
    if (lib.length != 1) { return undefined; }
    return lib.text();
  };
  logger.start();
  $('.import-test').each(function (i, board) {
    try {
      logger.reset();
      Board.evaluate(Board.parse($(board).text(), importer));
      Ω.equal(logger.buffer(),
              $(board).attr('should-log'),
              $(board).attr('assertion'));
    } catch (e) {
      Ω.ok(false, $(board).attr('assertion') + ": threw '"+e.toString()+"'");
    }
  });
  logger.stop();
});



QUnit.module("Color")
QUnit.test("color functions", function (Ω) {
	var n = 1000,
	    ep = 8;
	var close = function(x,y) {
		return Math.abs(x - y) <= ep;
	};
	var check = function(r,g,b,h,s,v,msg) {
		var rgb = {r:r, g:g, b:b};
		var hsv = color.rgb2hsv(rgb);
		var back = color.hsv2rgb(hsv);
		Ω.ok(close(hsv.h, h) && close(hsv.s, s) && close(hsv.v, v),
		     msg + ': rgb2hsv('+r+','+g+','+b+') should be hsv('+h+','+s+','+v+')');
	}
	check(185, 81,226,  200,164,226, 'spot check');
	check(242,127,128,  255,121,242, 'NaN case');
	check(206, 94, 95,  255,139,206, 'H=255 case');

	var randrgb = function() {
		return {
			r: parseInt(Math.random() * 255.0 + 0.5),
			g: parseInt(Math.random() * 255.0 + 0.5),
			b: parseInt(Math.random() * 255.0 + 0.5)
		};
	};
	var fails = 0;
	for (var i = 0; i < n; i++) {
		var rgb = randrgb();
		var hsv = color.rgb2hsv(rgb);
		var got = color.hsv2rgb(hsv);
		if (got == undefined) {
			Ω.ok(false, 'hsv2rgb(rgb2hsv(x)) is UNDEFINED');
		} else {
			Ω.ok(close(rgb.r, got.r) && close(rgb.g, got.g) && close(rgb.b, got.b),
					 'rgb('+rgb.r+','+rgb.g+','+rgb.b+') -> hsv('+hsv.h+','+hsv.s+','+hsv.v+') -> rgb('+got.r+','+got.g+','+got.b+')');
		}
	}
});
