;(function (document,window,undefined) {

var dedupe = function(q) {
	var seen = {};
	var refs = {};
	var uniq = {};

	$.each(q, function (k,v) {
		if (!(v in seen)) {
			seen[v] = k;
			uniq[k] = v;
		}
		refs[k] = seen[v];
	});
	return {
		unique:     uniq,
		references: refs,
	};
};

var ts = function (timestamp) {
	var d = new Date();
	d.setTime(timestamp);
	return d;
};

var keys = function (o) {
	var l = [];
	for (key in o) {
		l.push(key);
	}
	return l;
};

var only_metric = function (d, key) {
	if (key && key in d) {
		return d[key];
	}
	var kk = keys(d);
	if (kk.length == 0) {
		throw "No metric fields found in query results";
	}
	if (kk.length > 1) {
		throw "More than one metric field found in query results";
	}
	return d[kk[0]];
};

var sparkline = function (svg, data) {
	var bounds = svg.node().getBoundingClientRect(),
	    width  = bounds.width,
	    height = bounds.height;

	var x = d3.scaleTime()
	          .rangeRound([0, width])
	          .domain(d3.extent(data, function (d) { return d.t; }));

	var y = d3.scaleLinear()
	          .rangeRound([height, 0])
	          .domain(d3.extent(data, function (d) { return d.v; }));

	var line = d3.line()
	    .x(function(d) { return x(d.t); })
	    .y(function(d) { return y(d.v); })

	svg.append("path")
	   .datum(data)
	   .attr("d", line);
};

var init = (function () {
	var ID = 1;
	return function (o, attrs, type) {
		o.size = '3x3';
		for (key in attrs) { o[key] = attrs[key]; }
		o.id = 'block-'+ID.toString(); ID++;
		o.type = type;
	};
})();

var Block = function (attrs) { init(this, attrs, 'unknown') };

Block.prototype.html = function () {
	if (typeof(this.size) === 'undefined') {
		this.size = '3x3';
	}
	var dim = this.size.split('x')
	if (dim.length != 2) {
		throw "Invalid size '"+this.size+"'";
	}

	return '<div id="'+this.id+'" class="'+this.type+' w'+dim[0]+' h'+dim[1]+'"></div>';
};

Block.prototype.select = function () {
	return d3.select('#'+this.id);
};

Block.prototype.validate = function () { };
Block.prototype.parse    = function () { };
Block.prototype.update   = function () { };

var Break = function (attrs) { init(this, attrs, 'break') };
Break.prototype = Object.create(Block.prototype);
Break.prototype.html = function () {
	return '<br>';
}

var PlaceholderBlock = function (attrs) { init(this, attrs, 'placeholder') };
PlaceholderBlock.prototype = Object.create(Block.prototype);
PlaceholderBlock.prototype.update = function () {
	var svg    = this.select().append('svg'),
	    bounds = svg.node().getBoundingClientRect()
	    c      = color.spec(this.color);

	svg.style("background-color", c.bg)
	   .append("text")
	     .attr("x", bounds.width  / 2)
	     .attr("y", bounds.height / 2)
	     .style("fill", c.fg)
	     .text(this.text);
};

var MetricBlock = function (attrs) { init(this, attrs, 'metric') };
MetricBlock.prototype = Object.create(Block.prototype);
MetricBlock.prototype.update = function (data) {
	data = only_metric(data);
	var v = data[data.length-1].v;

	var root = this.select();

	root.append('p')
	    .text(d3.format(".2")(v))
	    .append('span')
	      .text(this.unit);
	root.append('h2')
	    .text(this.label);

	var c;
	if (typeof(this.color) == 'object') {
		for (var i = 0; i < this.color.rules.length; i++) {
			var rule = this.color.rules[i];
			/* does the value match the threshold conditional? */
			if (rule.eval(v, rule.against)) {
				c = color.spec(rule.color);
				break;
			}
		}
	} else {
		c = color.spec(this.color || 'white/navy');
	}

	root.style('color',            c.fg)
	    .style('background-color', c.bg);

	if (this.graph) {
		sparkline(root.append('svg')
		              .style('stroke', c.fg), data);
	}
};

var SparklineBlock = function (attrs) { init(this, attrs, 'sparkline') };
SparklineBlock.prototype = Object.create(Block.prototype);
SparklineBlock.prototype.update = function (data) {
	data = only_metric(data, this.plot);

	var root   = this.select(),
	    svg    = root.append('svg'),
	    bounds = svg.node().getBoundingClientRect(),
	    margin = {top: 0, right: 0, bottom: 0, left: 0},
	    width  = bounds.width  - margin.left - margin.right,
	    height = bounds.height - margin.top  - margin.bottom,
	    frame  = svg.append("g")
	                .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

	var x = d3.scaleTime()
	    .rangeRound([0, width]);

	var y = d3.scaleLinear()
	    .rangeRound([height, 0]);

	var line = d3.line()
	    .x(function(d) { return x(d.t);  })
	    .y(function(d) { return y(d.v); })

	x.domain(d3.extent(data, function(d) { return d.t; }));
	y.domain([d3.min(data, function (d) { return d.v; }),
	          d3.max(data, function (d) { return d.v; })]);

	root.append("p")
	    .text(this.label);
	frame.append("path")
	     .datum(data)
	     .style("stroke", this.color)
	     .attr("d", line);
};

var GraphBlock = function (attrs) { init(this, attrs, 'graph') };
GraphBlock.prototype = Object.create(Block.prototype);
GraphBlock.prototype.update = function (data) {
	var svg    = this.select().append('svg'),
	    bounds = svg.node().getBoundingClientRect(),
	    margin = {top: 20, right: 20, bottom: 50, left: 70};

	/* adjust margins based on axis labeling */
	if (!this.axis.y.on) {
		margin.left -= 50;
	}
	if (!this.axis.x.on) {
		margin.bottom -= 30;
	}

	var width  = bounds.width  - margin.left - margin.right,
	    height = bounds.height - margin.top  - margin.bottom,
	    frame  = svg.append("g")
	                .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

	var x = d3.scaleTime()
	          .rangeRound([0, width]);

	var y = d3.scaleLinear()
	          .rangeRound([height, 0]);

	var dv = function (d) { return d.v };

	var area = d3.area()
	             .x(function(d) { return x(ts(d.t)); })
	             .y1(function(d) { return y(dv(d)); });
	var line = d3.line()
	             .x(function(d) { return x(ts(d.t)); })
	             .y(function(d) { return y(dv(d)); });

	var min = 0,
	    max = 0;
	for (var i = 0; i < this.plots.length; i++) {
		min = Math.min(min, d3.min(data[this.plots[i][0]], dv));
		max = Math.max(max, d3.max(data[this.plots[i][0]], dv));
	}

	x.domain(d3.extent(data[this.plots[0][0]], function(d) { return ts(d.t); }));
	y.domain([min, max]);
	area.y0(y(0));

	for (var i = 0; i < this.plots.length; i++) {
		var field = this.plots[i][0];
		var plot = this.plots[i][1];

		if (plot.as == 'line') {
			frame.append("path")
			     .datum(data[field])
			     .style("stroke-width", plot.width)
			     .style("stroke", plot.color)
			     .style("fill", "none")
			     .attr("d", line);
		} else {
			frame.append("path")
			     .datum(data[field])
			     .style("fill", plot.color)
			     .style("stroke-width", plot.width)
			     .style("stroke", color.darken(plot.color))
			     .attr("d", area);
		}
	}

	svg.append("g")
	   .attr("transform", "translate(" + (bounds.width / 2) + ",10)")
	   .append("text")
	      .attr("text-anchor", "middle")
	      .style("font-size", "10px") /* FIXME: can we move this to a stylesheet? */
	      .style("fill", "#000")
	      .text(this.label);

	svg.append("g")
	   .attr("transform", "translate(" + (bounds.width / 2) + "," + (bounds.height - 5) + ")")
	   .append("text")
	      .attr("text-anchor", "middle")
	      .style("font-size", "10px") /* FIXME: can we move this to a stylesheet? */
	      .style("fill", "#000")
	      .text(this.axis.x.label);
	if (this.axis.x.on) {
		frame.append("g")
		     .attr("transform", "translate(0," + height + ")")
		     .call(d3.axisBottom(x));
	}

	svg.append("g")
	   .attr("transform", "translate(15," + (bounds.height / 2) + ") rotate(-90)")
	   .append("text")
	      .attr("text-anchor", "middle")
	      .style("font-size", "10px") /* FIXME: can we move this to a stylesheet? */
	      .style("fill", "#000")
	      .text(this.axis.y.label);
	if (this.axis.y.on) {
		frame.append("g")
		     .call(d3.axisLeft(y));
	}
};

var ScatterPlotBlock = function (attrs) { init(this, attrs, 'scatterplot') };
ScatterPlotBlock.prototype = Object.create(Block.prototype);
ScatterPlotBlock.prototype.update = function (data) {
	var svg    = this.select().append('svg'),
	    bounds = svg.node().getBoundingClientRect(),
	    margin = {top: 20, right: 20, bottom: 40, left: 60};

	/* adjust margins based on axis labeling */
	if (!this.axis.y.on) {
		margin.left -= 40;
	}
	if (!this.axis.x.on) {
		margin.bottom -= 20;
	}

	var width  = bounds.width  - margin.left - margin.right,
	    height = bounds.height - margin.top  - margin.bottom,
	    frame  = svg.append("g")
	                .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

	var map = [];
	for (var i = 0; i < data[this.x].length; i++) {
		map[i] = {
			x: data[this.x][i].v,
			y: data[this.y][i].v
		};
	}

	var x = d3.scaleLinear()
	          .rangeRound([0, width])
	          .domain(d3.extent(map, function (d) { return d.x }));

	var y = d3.scaleLinear()
	          .rangeRound([height, 0])
	          .domain(d3.extent(map, function (d) { return d.y }));

	frame.selectAll('.dot')
	     .data(map)
	     .enter()
	       .append('circle')
	         .attr('class', 'dot')
	         .attr('r', 2)
	         .attr('cx', function (d) { return x(d.x) })
	         .attr('cy', function (d) { return y(d.y) })
	         .style('fill', this.color);

	svg.append("g")
	   .attr("transform", "translate(" + (bounds.width / 2) + ",10)")
	   .append("text")
	      .attr("text-anchor", "middle")
	      .style("font-size", "10px") /* FIXME: can we move this to a stylesheet? */
	      .style("fill", "#000")
	      .text(this.label);

	svg.append("g")
	   .attr("transform", "translate(" + (bounds.width / 2) + "," + (bounds.height - 5) + ")")
	   .append("text")
	      .attr("text-anchor", "middle")
	      .style("font-size", "10px") /* FIXME: can we move this to a stylesheet? */
	      .style("fill", "#000")
	      .text(this.axis.x.label);
	if (this.axis.x.on) {
		frame.append("g")
		     .attr("transform", "translate(0," + height + ")")
		     .call(d3.axisBottom(x));
	}

	svg.append("g")
	   .attr("transform", "translate(15," + (bounds.height / 2) + ") rotate(-90)")
	   .append("text")
	      .attr("text-anchor", "middle")
	      .style("font-size", "10px") /* FIXME: can we move this to a stylesheet? */
	      .style("fill", "#000")
	      .text(this.axis.y.label);
	if (this.axis.y.on) {
		frame.append("g")
		     .call(d3.axisLeft(y));
	}
};

var parse = function (s,prefix) {
	if (typeof(prefix) === 'undefined') {
		prefix = 'block';
	}

	var i    = 0, line = 1, column = 0,
	    last =  { line : 1, column : 0 },
	    isspace = ' \f\n\r\t\v\u00A0\u2028\u2029',
	    isalnum = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_@/#';

	const T_IDENTIFIER = 1;
	const T_STRING     = 2;
	const T_NUMERIC    = 3;
	const T_SIZE       = 4;
	const T_REF        = 5;
	const T_OPEN       = 6;
	const T_CLOSE      = 7;
	const T_COLON      = 8;
	const T_LE         = 9;
	const T_LT         = 10;
	const T_GE         = 11;
	const T_GT         = 12;
	const T_EQ         = 13;
	const T_NE         = 14;

	function token_description(t) {
		switch (t[0]) {
		case T_IDENTIFIER: return "an identifer (<tt>'"+t[1]+"'</tt>)";
		case T_STRING:     return "a string (<tt>'"+t[1]+"'</tt>)";
		case T_NUMERIC:    return "a number (<tt>"+t[1]+"</tt>)";
		case T_SIZE:       return "a block size (<tt>"+t[1]+"</tt>)";
		case T_REF:        return "a threshold reference (<tt>@"+t[1]+"</tt>)";
		case T_OPEN:       return "an opening curly brace (<tt>'{'</tt>)";
		case T_CLOSE:      return "a closing curly brace (<tt>'}'</tt>)";
		case T_COLON:      return "the rule separator, ':'";
		case T_LE:         return "the less-than-or-equal-to operator (<tt><=</tt>)";
		case T_LT:         return "the strictly-less-than operator (<tt><</tt>)";
		case T_GE:         return "the greater-than-or-equal-to operator (<tt>>=</tt>)";
		case T_GT:         return "the strictly-greater-than operator (<tt>></tt>)";
		case T_EQ:         return "the equality operator (<tt>=</tt>)";
		case T_NE:         return "the inequality operator (<tt>!=</tt>)";
		default:           return "an unrecognized token.  :/";
		}
	}

	var lex = function() {
		var j = 0;
		if (i >= s.length) { return undefined; }
		while (i < s.length && isspace.indexOf(s[i]) > -1) {
			i++; column++;
			if (s[i] == '\n') { line++; column = 0; }
		};
		if (i >= s.length) { return undefined; }

		/* store our pre-lexeme line/column, for error messages */
		last = { line: line, column: column };

		if (s[i] == '%') {
			while (i < s.length && s[i] != '\n') { i++; };
			line++; column = 0;
			return lex();
		}
		if (s[i] == '"') {
			j = i = i + 1; column++
			while (i < s.length && s[i] != '"') {
				i++; column++;
				if (s[i] == '\n') { line++; column = 0; }
			}
			i++; column++;
			return [T_STRING, s.substr(j, i-j-1)];
		}
		if (s[i] == '[') {
			j = i = i + 1; column++;
			while (i < s.length && s[i] != ']') {
				i++; column++;
				if (s[i] == '\n') { line++; column = 0; }
			}
			i++; column++;
			var str = s.substr(j, i-j-1)
					   .replace(/^\s+/, '')
					   .replace(/\s+$/, '')
					   .replace(/\n\s+/gm, ' ');
			return [T_STRING, str];
		}
		if (s[i] == '@') {
			j = i = i + 1; column++;
			while (i < s.length && isalnum.indexOf(s[i]) > -1) { i++; column++; };
			return [T_REF, s.substr(j, i-j)];
		}
		if (s[i] == '{') { i++; column++; return [T_OPEN];  }
		if (s[i] == '}') { i++; column++; return [T_CLOSE]; }
		if (s[i] == ':') { i++; column++; return [T_COLON]; }
		if (s[i] == '<') { i++; column++; if (s[i] == '=') { i++; column++; return [T_LE] }; return [T_LT]; }
		if (s[i] == '>') { i++; column++; if (s[i] == '=') { i++; column++; return [T_GE] }; return [T_GT]; }
		if (s[i] == '=') { i++; column++; return [T_EQ]; }
		if (s[i] == '!') { i++; column++; if (s[i] == '=') { i++; column++; return [T_NE] };
			console.log('failed'); return undefined; }

		j = i;
		while (i < s.length && isalnum.indexOf(s[i]) > -1) { i++; column++; };
		if (j == i) {
			throw with_lineno('I have no idea how to parse the rest of this code.');
		}
		var tok = s.substr(j, i-j);
		if (tok.match(/^([0-9]|1[0-2])x([0-9]|1[0-2])$/)) {
			return [T_SIZE, tok];
		}
		if (tok.match(/^[0-9]+(\.[0-9]+)?/)) {
			return [T_NUMERIC, tok];
		}
		return [T_IDENTIFIER, tok];
	}

	var thresholds = {};
	var blocks = [];

	var iskeyword = function (t, kw) {
		return t[0] == T_IDENTIFIER && t[1] == kw;
	}

	function with_lineno(message) {
		var j = i;
		while (j >= 0 && s[j] != '\n') { j-- }; j++;
		var k = i;
		while (k+1 < s.length && s[k+1] != '\n') { k++; }

		return message+'<br><br>(on line '+last.line+', character '+last.column+')<br>'+
				 '<xmp>'+s.substr(j,k).replace(/\t/g, ' ')+'\n'+
				 ' '.repeat(last.column - 1) + '^^^</xmp>';
	}
	function unexpected_token(what, got, want) {
		return with_lineno(
			'I had trouble '+what+';<br/>'+
			'I expected to find '+want+', but instead I found '+token_description(got));
	}
	function expect_open(what) {
		t = expect_token(what); if (t[0] == T_OPEN) { return t; }
		return unexpected_token(what, t, 'an opening curly brace, <tt>{</tt>');
	}
	function expect_token(what) {
		t = lex(); if (t) { return t; }
		throw with_lineno(
			'I had trouble '+what+';<br>'+
			'I ran out of code to parse!');
	}

	function validate_size(what, size) {
		if (!size) {
			throw 'I had issues '+what+';<br/>'+
				  'It looks like you specified an empty size!';
		}
		var c = size.split('x');
		c[0] = parseInt(c[0]);
		c[1] = parseInt(c[1]);
		if (isNaN(c[0]) || c[0] < 1 || isNaN(c[1]) || c[1] < 1) {
			throw 'I had issues '+what+';<br/>'+
				  "It looks like you specified an invalid size of '<tt>"+size+"</tt>'";
		}
		if (c[0] > 12) {
			throw 'I had issues '+what+';<br/>'+
				  "It looks like you specified an invalid size of '<tt>"+size+"</tt>' (width cannot exceed '12')";
		}
	}

	function validate_query(what, q) {
		q = q.replace(/^\s+|\s+$/g, '');
		if (!q) {
			throw 'I encountered a problem '+what+';<br/>'+
				  'You seem to have forgotten to specify a query!';
		}
	}

	var parse_threshold = function() {
		var ctx = 'parsing a threshold name';
		var t;
		var o = {
			name: undefined,
			rules: []
		}

		t = expect_token(ctx);
		if (t[0] != T_STRING) { throw unexpected_token(ctx, t, 'a string value'); }
		o.name = t[1];

		ctx = 'parsing a threshold definition';
		expect_open(ctx);

		var R = function (fn,v,c) {
			return {
				eval:    fn,
				against: v,
				color:   c
			};
		};
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'when')) {
				var op = expect_token(ctx);
				switch (op[0]) {
				case T_GT: case T_GE:
				case T_LT: case T_LE:
				case T_EQ: case T_NE: break;
				default: throw unexpected_token(ctx, op, 'a comparison operator (<tt>&gt;</tt>, <tt>&gt;=</tt>, <tt>&lt;</tt>, <tt>&lt;=</tt>, <tt>=</tt>, or <tt>!=</tt>)');
				}

				var val = expect_token(ctx);
				if (val[0] != T_NUMERIC) { throw unexpected_token(ctx, val, 'a number'); }

				var c = expect_token(ctx);
				if (c[0] != T_COLON) { throw unexpected_token(ctx, val, 'the rule separator, <tt>:</tt>'); }

				var col = expect_token(ctx);
				if (col[0] != T_STRING && col[0] != T_IDENTIFIER) { throw unexpected_token(ctx, col, 'a color name, either as a quoted string, or a bare word.'); }

				var f = parseFloat(val[1]);
				switch (op[0]) {
				case T_GT: o.rules.push(R(function (v,f) { return v >  f }, f, col[1])); break;
				case T_GE: o.rules.push(R(function (v,f) { return v >= f }, f, col[1])); break;
				case T_LT: o.rules.push(R(function (v,f) { return v <  f }, f, col[1])); break;
				case T_LE: o.rules.push(R(function (v,f) { return v <= f }, f, col[1])); break;
				case T_EQ: o.rules.push(R(function (v,f) { return v == f }, f, col[1])); break;
				case T_NE: o.rules.push(R(function (v,f) { return v != f }, f, col[1])); break;
				}
				f = 32;
				continue;
			}
			if (iskeyword(t, 'default')) {
				var c = expect_token(ctx);
				if (c[0] != T_COLON) { throw unexpected_token(ctx, val, 'the rule separator, <tt>:</tt>'); }

				var col = expect_token(ctx);
				if (col[0] != T_STRING && col[0] != T_IDENTIFIER) { throw unexpected_token(ctx, col, 'a color name, either as a quoted string, or a bare word.'); }

				o.rules.push(R(function (v) { return true }, undefined, col[1]));
				continue;
			}
			if (t[0] == T_CLOSE) {
				if (o.rules.length == 0) {
					throw "I couldn't make sense of your threshold;<br>"+
						  "You didn't specify any rules!";
				}
				return o;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or another threshold rule');
		}
	}

	var parse_metric = function() {
		var ctx = 'parsing a metric definition';
		var t, o = new MetricBlock({
			size:  '3x3',
			unit:  '',
			label: '',
			color: ''
		});

		expect_open(ctx);
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
				iskeyword(t, 'unit')  ||
				iskeyword(t, 'label') ||
				iskeyword(t, 'graph') ||
				iskeyword(t, 'query')
			) {
				var val = expect_token(ctx);
				o[t[1]] = val[1];
				continue;
			}
			if (iskeyword(t, 'color')) {
				var val = expect_token(ctx);
				if (val[0] == T_REF) {
					o[t[1]] = thresholds[val[1]];
				} else {
					o[t[1]] = val[1];
				}
				continue;
			}
			if (t[0] == T_CLOSE) {
				validate_size(ctx, o.size);
				validate_query(ctx, o.query);
				return o;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}

	var parse_sparkline = function() {
		var ctx = 'parsing a sparkline definition';
		var t, o = new SparklineBlock({
			label: 'My Unnamed Sparkline',
			size:  '12x1',
			color: 'black'
		});

		expect_open(ctx);
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
				iskeyword(t, 'label') ||
				iskeyword(t, 'color') ||
				iskeyword(t, 'query') ||
				iskeyword(t, 'plot')
			) {
				var val = expect_token(ctx);
				o[t[1]] = val[1];
				continue;
			}
			if (t[0] == T_CLOSE) {
				validate_size(ctx, o.size);
				validate_query(ctx, o.query);
				return o;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}

	var parse_plot = function () {
		var ctx = 'parsing a plot for a graph definition';
		var t, o = {
			type:  'plot',
			as:    'area',
			color: 'skyblue',
			width: '2'
		};
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'as')  ||
				iskeyword(t, 'color') ||
				iskeyword(t, 'width')
			) {
				var val = expect_token(ctx);
				o[t[1]] = val[1];
				continue;
			}
			if (t[0] == T_CLOSE) {
				if (o.as != 'line' && o.as != 'area') {
					throw 'I ran into problems '+ctx+';<br/>'+
						  "You used an invalid value for the plot type ('<tt>"+o.as+"</tt>') -- it should be either 'line' or 'area'";
				}
				return o;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}

	var parse_axis = function (lead, axis) {
		var ctx = 'parsing an axis formatter';
		var t, val;

		lead = lead.substr(0,1);

		t = expect_token(ctx);
		if (iskeyword(t, 'label')) {
			if (lead == 'a') {
				/* FIXME: do we have an error for semantic issues? */
				throw unexpected_token(ctx, t, 'one of either <tt>on</tt>, or <tt>off</tt>');
			}
			val = expect_token(ctx);
			axis[lead].label = val[1];
			return axis;
		}

		if (iskeyword(t, 'on') || iskeyword(t, 'yes')) {
			if (lead == 'a' || lead == 'x') { axis.x.on = true; }
			if (lead == 'a' || lead == 'y') { axis.y.on = true; }
			return axis;
		}

		if (iskeyword(t, 'off') || iskeyword(t, 'no')) {
			if (lead == 'a' || lead == 'x') { axis.x.on = false; }
			if (lead == 'a' || lead == 'y') { axis.y.on = false; }
			return axis;
		}

		throw unexpected_token(ctx, t, 'an axis sub-command keyword');
	};

	var parse_graph = function () {
		var ctx = 'parsing a graph definition';
		var t, o = new GraphBlock({
			size:  '4x3',
			label: '',
			plots: [],
			axis: {
				x: { on: true, label: '' },
				y: { on: true, label: '' }
			}
		});

		expect_open(ctx);
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
				iskeyword(t, 'label') ||
				iskeyword(t, 'query')
			) {
				var val = expect_token(ctx);
				o[t[1]] = val[1];
				continue;
			}
			if (iskeyword(t, 'x-axis') ||
			    iskeyword(t, 'y-axis') ||
			    iskeyword(t, 'axis')
			) {
				o.axis = parse_axis(t[1], o.axis);
				continue;
			}
			if (iskeyword(t, 'plot')) {
				t = expect_token(ctx);
				var field = undefined;
				if (t[0] == T_STRING) {
					field = t[1]
					t = expect_token(ctx);
				}
				if (t[0] != T_OPEN) { throw unexpected_token(ctx, t, 'an opening curly brace, <tt>{</tt>'); }
				o.plots.push([field, parse_plot()])
				continue;
			}
			if (t[0] == T_CLOSE) {
				validate_size(ctx, o.size);
				validate_query(ctx, o.query);
				return o;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}

	var parse_scatterplot = function () {
		var ctx = 'parsing a scatterplot definition';
		var t, o = new ScatterPlotBlock({
			size:  '4x3',
			label: '',
			color: '#000',
			axis: {
				x: { on: true, label: '' },
				y: { on: true, label: '' }
			}
		});

		expect_open(ctx);
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
				iskeyword(t, 'label') ||
				iskeyword(t, 'x') ||
				iskeyword(t, 'y') ||
				iskeyword(t, 'color') ||
				iskeyword(t, 'query')
			) {
				var val = expect_token(ctx);
				o[t[1]] = val[1];
				continue;
			}
			if (iskeyword(t, 'x-axis') ||
			    iskeyword(t, 'y-axis') ||
			    iskeyword(t, 'axis')
			) {
				o.axis = parse_axis(t[1], o.axis);
				continue;
			}
			if (t[0] == T_CLOSE) {
				validate_size(ctx, o.size);
				validate_query(ctx, o.query);
				return o;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}

	var parse_placeholder = function() {
		var ctx = 'parsing a placeholder definition';
		var t, o = new PlaceholderBlock({
			size:  '3x3',
			text:  'placeholder',
			color: '#ccc'
		});

		expect_open(ctx);
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'text')  ||
				iskeyword(t, 'size')  ||
				iskeyword(t, 'color')
			) {
				var val = expect_token(ctx);
				o[t[1]] = val[1];
				continue;
			}
			if (t[0] == T_CLOSE) {
				validate_size(ctx, o.size);
				return o;
			}
			throw unexpected_token(ctx, t, 'either a closing brace, <tt>}</tt>, or a parameter keyword (i.e. text, size, color)');
		}
	}

	var b, t, o,
	    thresholds = {},
	    blocks     = [],
	    queries    = {},
	    n = 0;
	while (typeof(t = lex()) !== 'undefined') {
		if (iskeyword(t, 'threshold')) {
			o = parse_threshold();
			thresholds[o.name] = o;
			continue;
		}
		if (iskeyword(t, 'metric')) {
			b = parse_metric();
			n++;
			blocks.push(b);
			queries[b.id] = b.query;
			continue;
		}
		if (iskeyword(t, 'sparkline')) {
			b = parse_sparkline();
			n++;
			blocks.push(b);
			queries[b.id] = b.query;
			continue;
		}
		if (iskeyword(t, 'break')) {
			blocks.push(new Break());
			continue;
		}
		if (iskeyword(t, 'graph')) {
			b = parse_graph();
			n++;
			blocks.push(b);
			queries[b.id] = b.query;
			continue;
		}
		if (iskeyword(t, 'scatterplot')) {
			b = parse_scatterplot();
			n++;
			blocks.push(b);
			queries[b.id] = b.query;
			continue;
		}
		if (iskeyword(t, 'placeholder')) {
			n++;
			blocks.push(parse_placeholder());
			continue;
		}
		if (t[0] == T_IDENTIFIER) {
			throw with_lineno("Uh-oh.  I don't recognize the block type '"+t[1]+"'");
		}
		throw unexpected_token('parsing your board', t, "a block keyword (like '<tt>metric</tt>' or '<tt>graph</tt>')");
	}

	return blocks;
};

var Board = (function () {
	var ID = 0;
	return function (link, name, code) {
		if (arguments.length == 1) {
			code = link;
			link = name = undefined;
		}

		this.code = code;
		this.blocks = parse(code);
		this.deleted = false;
		this.id = 'board-'+ID.toString();

		this.name = name || 'Board '+ID.toString();
		this.link = link || this.id;
		this.href = '#' + this.link;
		ID++;
	};
})();

Board.prototype.update = function (link, name, code) {
	this.code = code;
	this.blocks = parse(code);

	this.name = name || 'Board '+ID.toString();
	this.link = link || this.id;
};

Board.prototype.draw = function (root) {
	var q = {};

	for (var i = 0; i < this.blocks.length; i++) {
		var blk = this.blocks[i];
		if (blk.query) { q[blk.id] = blk.query; }
	}

	var self = this;
	q = dedupe(q);
	$.ajax({
		type:        'POST',
		url:         '/v1/query',
		contentType: 'application/json; charset=utf8',
		dataType:    'json',
		processData: false,
		data:        JSON.stringify(q.unique),

		success: function (data) {
			$(root).empty()
			       .attr('board-id', self.id)
			       .removeClass('deleted live')
			       .addClass(self.deleted ? 'deleted' : 'live')
			       .append('<div class="w12 deleted">This board has been deleted.  <a href="#" rel="undo">undo</a>.</div>');

			for (var i = 0; i < self.blocks.length; i++) {
				var blk = self.blocks[i];
				$(root).append(blk.html());
				blk.update(data[q.references[blk.id]]);
			}
		},
		error: function (r) {
			$(root).error("Oops.  Something's wrong with one of your queries...",
			                error_from(r.responseText).replace(/\n/g, '<br>'));
		}
	});
};

Board.lookup = function (fn) {
	$.ajax({
		type: 'GET',
		url:  '/v1/boards',
		success: function (data) {
			var list = [];
			for (var i = 0; i < data.length; i++) {
				list.push(new Board(
					data[i].link,
					data[i].name,
					data[i].code
				));
			}
			fn(list, data.access);
		}
		/* FIXME: what about on error? */
	});
};

window.Board = Board;

})(document,window);
