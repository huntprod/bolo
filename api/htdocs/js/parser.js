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

var as_self = function (x) { return x; };

var as_bytes = function (b) {
	var fmt = d3.format('.1f');
	if (b < 1024) { return fmt(b) + ' B';  } b /= 1024;
	if (b < 1024) { return fmt(b) + ' kB'; } b /= 1024;
	if (b < 1024) { return fmt(b) + ' MB'; } b /= 1024;
	                return fmt(b) + ' GB';
}

var as_si = function (n) {
	var fmt = d3.format('.1f');
	if (n < 1000) { return fmt(n) + '';  } n /= 1000;
	if (n < 1000) { return fmt(n) + 'k'; } n /= 1000;
	if (n < 1000) { return fmt(n) + 'M'; } n /= 1000;
	                return fmt(n) + 'B';
}

var as_whole = function (n) {
	if (Math.floor(n) != n) {
		return '';
	}
	return n;
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

var HTMLContentBlock = function (attrs) { init(this, attrs, 'html') };
HTMLContentBlock.prototype = Object.create(Block.prototype);
HTMLContentBlock.prototype.update = function (data) {
	this.select().html(this.content);
};

var TextContentBlock = function (attrs) { init(this, attrs, 'html') };
TextContentBlock.prototype = Object.create(Block.prototype);
TextContentBlock.prototype.update = function (data) {
	this.select().text(this.content);
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

	/* normalize stacks */
	var stacks = {};
	var running = [];
	for (var i = 0; i < this.plots.length; i++) {
		var plot = this.plots[i];

		if (plot.as != 'stack') { continue; }
		var s = []
		if (running.length == 0) {
			for (var j = 0; j < data[plot.field].length; j++) {
				s[j] = {t: data[plot.field][j].t,
				        v: data[plot.field][j].v};
			}
		} else {
			for (var j = 0; j < data[plot.field].length; j++) {
				s[j] = {t: data[plot.field][j].t,
				        v: data[plot.field][j].v
				         + running[j].v};
			}
		}
		stacks[plot.field] = s;
		running = s;
	}

	var min = 0,
	    max = 0;
	for (var i = 0; i < this.plots.length; i++) {
		min = Math.min(min, d3.min(data[this.plots[i].field], dv));
		max = Math.max(max, d3.max(data[this.plots[i].field], dv));
	}
	for (var k in stacks) {
		min = Math.min(min, d3.min(stacks[k], dv));
		max = Math.max(max, d3.max(stacks[k], dv));
	}

	x.domain(d3.extent(data[this.plots[0].field], function(d) { return ts(d.t); }));
	y.domain([min, max]);
	area.y0(y(0));

	for (var i = this.plots.length - 1; i >= 0; i--) {
		var plot = this.plots[i];

		switch (plot.as) {
		case 'stack':
			//console.log("%s data: %v", plot.as, data[plot.field]);
			frame.append("path")
			     .datum(stacks[plot.field])
			     .style("fill", plot.color)
			     .style("stroke-width", plot.width)
			     .style("stroke", color.darken(plot.color))
			     .style("opacity", plot.opacity / 100.0)
			     .attr("d", area);
			break;
		}
	}
	for (var i = this.plots.length - 1; i >= 0; i--) {
		var plot = this.plots[i];

		switch (plot.as) {
		case 'line':
			//console.log("%s data: %v", plot.as, data[plot.field]);
			frame.append("path")
			     .datum(data[plot.field])
			     .style("stroke-width", plot.width)
			     .style("stroke", plot.color)
			     .style("fill", "none")
			     .style("opacity", plot.opacity / 100.0)
			     .attr("d", line);
			break;

		case 'area':
			//console.log("%s data: %v", plot.as, data[plot.field]);
			frame.append("path")
			     .datum(data[plot.field])
			     .style("fill", plot.color)
			     .style("stroke-width", plot.width)
			     .style("stroke", color.darken(plot.color))
			     .style("opacity", plot.opacity / 100.0)
			     .attr("d", area);
			break;
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
		     .call(d3.axisLeft(y)
		             .tickFormat(this.axis.y.fmt || as_self));
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
		     .call(d3.axisBottom(x)
		             .tickFormat(this.axis.x.fmt || as_self));
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
		     .call(d3.axisLeft(y)
		             .tickFormat(this.axis.y.fmt || as_self));
	}
};

const T_IDENTIFIER  =  1;
const T_STRING      =  2;
const T_NUMERIC     =  3;
const T_SIZE        =  4;
const T_REF         =  5;
const T_OPEN        =  6;
const T_CLOSE       =  7;
const T_COLON       =  8;
const T_LE          =  9;
const T_LT          = 10;
const T_GE          = 11;
const T_GT          = 12;
const T_EQ          = 13;
const T_NE          = 14;
const T_OPEN_BCOM   = 15;
const T_CLOSE_BCOM  = 16;
const T_ASSIGN      = 17;
const T_VAR         = 18;
const T_OPEN_PAREN  = 19;
const T_CLOSE_PAREN = 20;
const T_COMMA       = 21;

var World = function () {
	var $w = function() {
		this.env = [];
		this.scope = -1;

		this.funs = {};
		this.ctx = '';

		this.blocks = [];
		this.board  = [];

		this.enter();
		this.define('main', []);
		return this;
	};

	$w.prototype.define = function (id, formals) {
		this.ctx = id;
		this.funs[id] = {
			args: formals,
			ops:  []
		};
	};
	$w.prototype.enter = function () {
		this.env[++this.scope] = {
			var:   {}, /* variable bindings */
			thold: {}  /* defined thresholds */
		};
	};
	$w.prototype.leave = function () {
		this.env[this.scope--] = undefined;
	};
	$w.prototype.op = function () {
		this.funs[this.ctx].ops.push(Array.prototype.slice.call(arguments));
	};
	$w.prototype.get = function (type, id) {
		if (!(type in {var:1,thold:1})) {
			throw 'I seem to be having some internal issues.<br>'+
						'Called world.get() with type "'+type+'", which is invalid.<br>'+
						'Please file a bug report.';
		}
		for (var i = this.scope; i >= 0; i--) {
			if (id in this.env[i][type]) {
				return this.valueof(this.env[i][type][id]);
			}
		}
		if (type == "var") {
			throw 'I was unable to find the variable <tt>$'+id+'</tt><br>'+
						'Did you forget to declare it?';
		}
		throw 'No such '+type+' binding, <tt>'+id+'</tt>';
	};
	$w.prototype.declare = function (type, id, value) {
		if (!(type in {var:1,thold:1})) {
			throw 'I seem to be having some internal issues.<br>'+
						'Called world.declare() with type "'+type+'", which is invalid.<br>'+
						'Please file a bug report.';
		}
		this.env[this.scope][type][id] = value;
	};
	$w.prototype.bind = function (type, id, value) {
		if (!(type in {var:1,thold:1})) {
			throw 'I seem to be having some internal issues.<br>'+
						'Called world.bind() with type "'+type+'", which is invalid.<br>'+
						'Please file a bug report.';
		}
		for (var i = this.scope; i >= 0; i--) {
			if (!(id in this.env[i][type])) { continue; }
			this.env[i][type][id] = value;
			return;
		}

		throw "No such "+type+" binding: <tt>"+id+"</tt>";
	};
	$w.prototype.valueof = function (thing) {
		switch (thing[0]) {
		case T_NUMERIC:
		case T_SIZE:
		case T_IDENTIFIER:
			return thing[1];

		case T_STRING:
			var s = '';
			for (var i = 0; i < thing[1].length; i++) {
				if (typeof(thing[1][i]) === 'string') {
					s += thing[1][i];
				} else {
					s += this.get('var', thing[1][i].ref);
				}
			}
			return s;

		case T_VAR:
			return this.get('var', thing[1]);
		}
	};

	$w.prototype.eval = function (id) {
		var fn = this.funs[id];
		for (var i = 0; i < fn.ops.length; i++) {
			fn.ops[i][0](this, fn.ops[i]);
		}
		return this.board;
	};

	return $w;
}();

var parse = function (s) {
	if (typeof(s) === 'undefined') { s = ''; }

	var i    = 0, line = 1, column = 0,
	    last =  { line : 1, column : 0 },
	    isspace = ' \f\n\r\t\v\u00A0\u2028\u2029',
	    isalnum = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_@/#-',
	    isvar   = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-';

	/* token_description(token) {{{ */
	function token_description(t) {
		switch (t[0]) {
		case T_IDENTIFIER:  return "an identifer (<tt>'"+t[1]+"'</tt>)";
		case T_STRING:      return "a string (<tt>'"+t[1]+"'</tt>)";
		case T_NUMERIC:     return "a number (<tt>"+t[1]+"</tt>)";
		case T_SIZE:        return "a block size (<tt>"+t[1]+"</tt>)";
		case T_REF:         return "a threshold reference (<tt>@"+t[1]+"</tt>)";
		case T_OPEN:        return "an opening curly brace (<tt>'{'</tt>)";
		case T_CLOSE:       return "a closing curly brace (<tt>'}'</tt>)";
		case T_COLON:       return "the rule separator, ':'";
		case T_LE:          return "the less-than-or-equal-to operator (<tt><=</tt>)";
		case T_LT:          return "the strictly-less-than operator (<tt><</tt>)";
		case T_GE:          return "the greater-than-or-equal-to operator (<tt>>=</tt>)";
		case T_GT:          return "the strictly-greater-than operator (<tt>></tt>)";
		case T_EQ:          return "the equality operator (<tt>=</tt>)";
		case T_NE:          return "the inequality operator (<tt>!=</tt>)";
		case T_OPEN_BCOM:   return "block comment opening sequence (<tt>/*</tt>)";
		case T_CLOSE_BCOM:  return "block comment closing sequence (<tt>*/</tt>)";
		case T_ASSIGN:      return "the assignment operator (<tt>:=</tt>)";
		case T_VAR:         return "a variable (<tt>$"+t[1]+"</tt>)";
		case T_OPEN_PAREN:  return "an opening parenthesis (<tt>(</tt>)";
		case T_CLOSE_PAREN: return "a closing parenthesis (<tt>)</tt>)";
		case T_COMMA:       return "a comma (<tt>,</tt>)";
		default:            return "an unrecognized token ("+t+") :/";
		}
	}
	/* }}} */
	/* interp("...") {{{ */
	var interp = (function () {
		return function (s) {
			var i = 0;
			var lex = function () {
				if (i >= s.length) { return undefined; }
				if (s[i] == '$') {
					/* variable */
					var j = 0, k = 0, simple = true;

					i++;
					if (i >= s.length) {
						throw "Unexpected end of variable reference; did you meant to escape the trailing '$'?";
					}

					/* differentiate ${...} from $... */
					if (s[i] == '{') { i++; simple = false; }
					j = i;
					while (i < s.length && isvar.indexOf(s[i]) > -1) { i++; }
					k = i;
					if (!simple) {
						if (i >= s.length) { throw "Unterminated bracketed variable reference ${...}"; }
						if (s[i] != '}') {
							/* malformed variable name; grab the whole thing and error */
							while (i < s.length && s[i] != '}') { i++; }
							if (i >= s.length) { throw "Unterminated bracketed variable reference ${...}"; }
							throw "Malformed variable name ${"+s.substr(j,i-j-1)+"}";
						}
					}

					/* return a ref */
					return {ref: s.substr(j, k-j)};
				}

				if (s[i] == '\\') {
					i++;
					if (i >= s.length) {
						throw "Unexpected escape character at end of string; did you meant to escape the trailing '\\'?";
					}
					return s[i];
				}

				var j = i;
				while (i < s.length && s[i] != '\\' && s[i] != '$') { i++; }
				return s.substr(j, i-j);
			};

			var parts = [], part;
			while ((part = lex()) != undefined) {
				parts.push(part);
			}
			return parts;
		};
	})();
	/* }}} */
	/* lex() {{{ */
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
			return [T_STRING, interp(s.substr(j, i-j-1), '"')];
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
			return [T_STRING, interp(str, '[')];
		}
		if (s[i] == '$') {
			j = i = i + 1; column++;
			while (i < s.length && isalnum.indexOf(s[i]) > -1) { i++; column++; };
			return [T_VAR, s.substr(j, i-j)];
		}
		if (s[i] == '@') {
			j = i = i + 1; column++;
			while (i < s.length && isalnum.indexOf(s[i]) > -1) { i++; column++; };
			return [T_REF, s.substr(j, i-j)];
		}
		if (s[i] == ',') { i++; column++; return [T_COMMA];  }
		if (s[i] == '{') { i++; column++; return [T_OPEN];  }
		if (s[i] == '}') { i++; column++; return [T_CLOSE]; }
		if (s[i] == '(') { i++; column++; return [T_OPEN_PAREN];  }
		if (s[i] == ')') { i++; column++; return [T_CLOSE_PAREN]; }
		if (s[i] == ':') { i++; column++; if (s[i] == '=') { i++; column++; return [T_ASSIGN]; }; return [T_COLON]; }
		if (s[i] == '<') { i++; column++; if (s[i] == '=') { i++; column++; return [T_LE]; }; return [T_LT]; }
		if (s[i] == '>') { i++; column++; if (s[i] == '=') { i++; column++; return [T_GE]; }; return [T_GT]; }
		if (s[i] == '=') { i++; column++; return [T_EQ]; }
		if (s[i] == '!') { i++; column++; if (s[i] == '=') { i++; column++; return [T_NE]; };
			console.log('failed'); return undefined; }
		if (s[i] == '/' && s[i+1] == '*') { i += 2; column += 2; return [T_OPEN_BCOM];  }
		if (s[i] == '*' && s[i+1] == '/') { i += 2; column += 2; return [T_CLOSE_BCOM]; }

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
	/* }}} */

	/* iskeyword(t, "keyword") {{{ */
	var iskeyword = function (t, kw) {
		return t[0] == T_IDENTIFIER && t[1] == kw;
	}
	/* }}} */
	/* with_lineno("what failed...") {{{ */
	function with_lineno(message) {
		var j = i;
		while (j >= 0 && s[j] != '\n') { j-- }; j++;
		var k = i;
		while (k+1 < s.length && s[k+1] != '\n') { k++; }

		var pad = last.column > 1 ? last.column - 1 : 0;
		return message+'<br><br>(on line '+last.line+', character '+last.column+')<br>'+
				 '<xmp>'+s.substr(j,k).replace(/\t/g, ' ')+'\n'+
				 ' '.repeat(pad) + '^^^</xmp>';
	}
	/* }}} */
	/* unexpected_token("what you were doing", got, wanted) {{{ */
	function unexpected_token(what, got, want) {
		return with_lineno(
			'I had trouble '+what+';<br/>'+
			'I expected to find '+want+', but instead I found '+token_description(got));
	}
	/* }}} */
	/* expect_open("what you were doing") {{{ */
	function expect_open(what) {
		t = expect_token(what); if (t[0] == T_OPEN) { return t; }
		return unexpected_token(what, t, 'an opening curly brace, <tt>{</tt>');
	}
	/* }}} */
	/* next() {{{ */
	function next() {
		while (true) {
			t = lex();
			if (!t) { return undefined; }

			switch (t[0]) {
			case T_OPEN_BCOM:
				var depth = 1;
				while (depth > 0) {
					t = lex();
					if (!t) {
						throw with_lineno(
							'I had trouble with a block comment (<tt>/* ... */</tt>)<br>'+
							'I ran out of code to parse!');
					}
					switch (t[0]) {
					case T_OPEN_BCOM:  depth++; break;
					case T_CLOSE_BCOM: depth--; break;
					}
				}
				break;

			default:
				if (iskeyword(t, "let")) {
					parse_letvar(world);
				} else {
					return t;
				}
				break;
			}
		}
	}
	/* }}} */
	/* expect_token("what you were doing") {{{ */
	function expect_token(what) {
		t = next();
		if (t) { return t; }
		throw with_lineno(
			'I had trouble '+what+';<br>'+
			'I ran out of code to parse!');
	}
	/* }}} */

	/** OP_DECLARE {{{

	    (declare varname)

	    Declare a variable slot in the current scope,
	    without initializing it.  (OP_ASSIGN will do that)

	 **/
	var OP_DECLARE = function (world, op) {
		world.declare('var', op[1], undefined);
	};
	/* }}} */
	/** OP_ASSIGN {{{

	    (assign varname value)

	    Assign a value to the named variable slot closest
	    to our current scope.

	    It is an error to assign to a variable that has not
	    been previously declared via OP_DECLARE.

	 **/
	var OP_ASSIGN = function (world, op) {
		try {
			world.bind('var', op[1], op[2]);
		} catch (e) {
			throw "Undefined variable <tt>$"+op[1]+"</tt>";
		}
	};
	/* }}} */
	/** OP_CALL {{{

	    (call fn args)

	    Applies a function to a set of arguments, evaluating for side
	    effects.  The functional evaluation occurs in a new dynamic
	    scope to limit damage to the calling environment.
	 **/
	var OP_CALL = function (world, op) {
		/* implicit scope push */
		world.enter();

		var fn = world.funs[op[1]];
		var args = op[2];

		/* check arity at runtime */
		if (args.length != fn.args.length) {
			switch (fn.args.length) {
			case 0:
				throw "Arity mismatch in call to "+op[1]+"();<br/>"+
				      "The "+op[1]+" function does not take any arguments, but "+args.length+" were given.";
			case 1:
				throw "Arity mistmatch in call to "+op[1]+"(...);<br/>"+
				      "The "+op[1]+" function takes one argument, but was given "+args.length;
			default:
				throw "Arity mistmatch in call to "+op[1]+"(...);<br/>"+
				      "The "+op[1]+" function takes "+fn.args.length+" arguments, but was given "+args.length;
			}
		}

		/* enrich the called environment */
		for (var i = 0; i < args.length; i++) {
			world.declare('var', fn.args[i], args[i]);
		}

		/* evaluate the the function body */
		world.eval(op[1]);

		/* pop the scope */
		world.leave();
	};
	/* }}} */
	/** OP_START {{{

	    (start type)

	    Opens a new object, of the given type, for modification.
	    This operator also implicitly pushes new scope.

	 **/
	var OP_START = function (world, op) {
		/* implicit scope push */
		world.enter();

		var must_be_toplevel = function () {
			if (world.blocks.length != 0) {
				throw "Illegal nesting of a '"+op[1]+"' block inside of another block";
			}
		};

		switch (op[1]) {
		default:
			throw "Unrecognized block type: '"+op[1]+"'";

		case 'break':
			must_be_toplevel();
			world.blocks.push(new Break());
			break;

		case 'threshold':
			must_be_toplevel();
			break;
			world.current_thold = {rules: []};
			world.declare('thold', op[1], world.current_thold);
			break;

		case 'metric':
			must_be_toplevel();
			world.blocks.push(new MetricBlock({
				size:  '3x3',
				unit:  '',
				label: '',
				color: ''
			}));
			break;

		case 'sparkline':
			must_be_toplevel();
			world.blocks.push(new SparklineBlock({
				label: 'My Unnamed Sparkline',
				size:  '12x1',
				color: 'black'
			}));
			break;

		case 'graph':
			must_be_toplevel();
			world.blocks.push(new GraphBlock({
				size:  '4x3',
				label: '',
				plots: [],
				axis: {
					x: { on: true, label: '' },
					y: { on: true, label: '' }
				}
			}));
			break;

		case 'scatterplot':
			must_be_toplevel();
			world.blocks.push(new ScatterPlotBlock({
				size:  '4x3',
				label: '',
				color: '#000',
				axis: {
					x: { on: true, label: '' },
					y: { on: true, label: '' }
				}
			}));
			break;

		case 'plot':
			if (world.blocks.length != 1) {
				throw "Illegal placement of 'plot' block";
			}
			world.blocks.push({
				type:    'plot',
				as:      'area',
				color:   'skyblue',
				width:   '2',
				opacity: 100.0
			});
			break;

		case 'placeholder':
			must_be_toplevel();
			world.blocks.push(new PlaceholderBlock({
				size:  '3x3',
				text:  'placeholder',
				color: '#ccc'
			}));
			break;

		case 'html':
			must_be_toplevel();
			world.blocks.push(new HTMLContentBlock({
				size:    '12x1',
				content: ''
			}));
			break;

		case 'text':
			must_be_toplevel();
			world.blocks.push(new TextContentBlock({
				size:    '12x1',
				content: ''
			}));
			break;
		}
	};
	/* }}} */
	/** OP_END {{{

	    (end)

	    Closes the open block and appends it to the list of
	    defined blocks, after invoking some validation logic
	    against it (type-sensitive).

	    This operator also implicitly pops the scope that was
	    pushed by its (start ...) counterpart.

	 **/
	var OP_END = function (world, op) {
		block = world.blocks.pop();

		if (world.blocks.length == 0) {
			world.board.push(block);
		} else {
			world.blocks[world.blocks.length - 1].plots.push(block);
		}

		/* pop the scope */
		world.leave();
	};
	/* }}} */
	/** OP_TRULE {{{
	 **/
	var OP_TRULE = function (world, cmp, val, color) {
		/* FIXME implement OP_TRULE */
	};
	/* }}} */
	/** OP_SET {{{

	    (set attr value)

	    Sets an attribute on the currently open block, possibly
	    by interrogating the environment (T_VARs) or interpolating
	    a string (T_STRING).

	 **/
	var OP_SET = function (world, op) {
		if (typeof(op[2]) !== 'object') {
			world.blocks[world.blocks.length - 1][op[1]] = op[2];
			return;
		}

		switch (op[2][0]) {
		default:
			throw 'I found a '+token_description(op[2])+' where I expected to find a number, a quoted string literal, or a variable reference';
			// FIXME line no?

		case T_NUMERIC:
		case T_SIZE:
		case T_IDENTIFIER:
			world.blocks[world.blocks.length - 1][op[1]] = op[2][1];
			break;

		case T_STRING:
			var s = '';
			for (var i = 0; i < op[2][1].length; i++) {
				if (typeof(op[2][1][i]) === 'string') {
					s += op[2][1][i];
				} else {
					s += world.get('var', op[2][1][i].ref);
				}
			}
			world.blocks[world.blocks.length - 1][op[1]] = s;
			break;

		case T_VAR:
			world.blocks[world.blocks.length - 1][op[1]] = world.get('var', op[2][1]);
			break;
		}
	};
	/* }}} */

	/* `size' validation routine {{{ */
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
	/* }}} */
	/* `query' validation routine {{{ */
	function validate_query(what, q) {
		q = q.replace(/^\s+|\s+$/g, '');
		if (!q) {
			throw 'I encountered a problem '+what+';<br/>'+
				  'You seem to have forgotten to specify a query!';
		}
	}
	/* }}} */

	/* variable declaration parser routine {{{ */
	var parse_letvar = function (world) {
		var ctx = 'parsing a variable definition';
		var t = expect_token(ctx);
		if (t[0] != T_IDENTIFIER) { throw unexpected_token(ctx, t, 'a variable name'); }

		world.op(OP_DECLARE, t[1]);
		parse_setvar(world, t[1], ctx);
	};
	/* }}} */
	/* variable assignment parser routine {{{ */
	var parse_setvar = function (world, vname, ctx) {
		var t;
		var v = undefined;
		if (typeof(ctx) === 'undefined') {
			ctx = 'parsing a variable assignment';
		}

		t = expect_token(ctx);
		if (t[0] != T_ASSIGN) { throw unexpected_token(ctx, t, 'the assignment operator (<tt>:=</tt>)'); }

		t = expect_token(ctx);
		switch (t[0]) {
		case T_STRING:
		case T_NUMERIC:
		case T_SIZE:
		case T_VAR:
			world.op(OP_ASSIGN, vname, t);
			return;
		}
		throw unexpected_token(ctx, t, 'a scalar value (a string, number, or a size)');
	};
	/* }}} */

	/* function definition parser routine {{{ */
	var parse_def = function(world, depth) {
		var t, ctx = 'parsing a function definition';

		t = expect_token(ctx);
		if (t[0] != T_IDENTIFIER) { throw unexpected_token(ctx, t, 'a function name'); }
		var fn = t[1];

		t = expect_token(ctx);
		if (t[0] != T_OPEN_PAREN) { throw unexpected_token(ctx, t, 'an opening parenthesis, <tt>(</tt>'); }

		// parse argument list
		var formals = [];
		for (;;) {
			t = expect_token(ctx);
			if (t[0] == T_CLOSE_PAREN) { break; }
			if (t[0] != T_VAR) { throw unexpected_token(ctx, t, 'a parameter name'); }
			formals.push(t[1]);

			t = expect_token(ctx);
			if (t[0] == T_CLOSE_PAREN) { break;    }
			if (t[0] == T_COMMA)       { continue; }
			throw unexpected_token(ctx, t, 'a comma, or a closing parenthesis');
		}

		var prev_ctx = world.ctx;
		world.define(fn, formals); /* sets world.ctx */

		// parse body of the function
		expect_open(ctx);
		parse_toplevel(world, depth+1);
		world.ctx = prev_ctx;
	}
	/* }}} */
	/* function call (application) parser routine {{{ */
	var parse_call = function(world, fn) {
		var t, ctx = 'parsing a functional call';
		t = expect_token();
		if (t[0] != T_OPEN_PAREN) { throw unexpected_token(ctx, t, 'an opening parenthesis, <tt>(</tt>'); }

		var args = [];
		for (;;) {
			t = expect_token(ctx);
			if (t[0] == T_CLOSE_PAREN) { break; }
			switch (t[0]) {
			default:
				throw unexpected_token(ctx, t, 'a literal value, or a variable reference');

			case T_VAR:
			case T_STRING:
			case T_NUMERIC:
			case T_SIZE:
			case T_IDENTIFIER:
				args.push(t);
				break;
			}

			t = expect_token(ctx);
			if (t[0] == T_CLOSE_PAREN) { break;    }
			if (t[0] == T_COMMA)       { continue; }
			throw unexpected_token(ctx, t, 'a comma, or a closing parenthesis');
		}

		world.op(OP_CALL, fn, args);
	}
	/* }}} */

	/* PLOT sub-block parser routine {{{ */
	var parse_plot = function (world, field) {
		var ctx = 'parsing a plot for a graph definition';
		var t, o = {
			type:    'plot',
			as:      'area',
			color:   'skyblue',
			width:   '2',
			opacity: 100.0
		};
		world.op(OP_START, 'plot');
		world.op(OP_SET, 'field', field);
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'as')    ||
			    iskeyword(t, 'color')   ||
			    iskeyword(t, 'opacity') ||
			    iskeyword(t, 'width')
			) {
				var val = expect_token(ctx);
				world.op(OP_SET, t[1], val);
				continue;
			}
			if (t[0] == T_CLOSE) {
				world.op(OP_END);
				return;
				/*
				if (o.as != 'line' && o.as != 'area' && o.as != 'stack') {
					throw 'I ran into problems '+ctx+';<br/>'+
						  "You used an invalid value for the plot type ('<tt>"+o.as+"</tt>') -- it should be either 'line', 'area', or 'stack'";
				}
				return o;
				*/
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}
	/* }}} */
	/* *AXIS field parser routine {{{ */
	var parse_axis = function (world, lead) {
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
			world.op(OP_SET, lead+'-label', val);
			return;
		}

		if (iskeyword(t, 'format')) {
			var fmt = as_self;
			val = expect_token(ctx);
			world.op(OP_SET, lead+'-format', val);
			return;
		}

		if (iskeyword(t, 'on') || iskeyword(t, 'yes')) {
			world.op(OP_SET, lead+'-on', 1);
			return;
		}

		if (iskeyword(t, 'off') || iskeyword(t, 'no')) {
			world.op(OP_SET, lead+'-on', 0);
			return;
		}

		throw unexpected_token(ctx, t, 'an axis sub-command keyword');
	};
	/* }}} */

	/* generic content block parser routine {{{ */
	var parse_content = function (world, ctx) {
		t = expect_token(ctx);
		switch (t[0]) {
		default:
			throw unexpected_token(ctx, t, 'a size, variable reference, or a quoted string literal');

		case T_SIZE:
			world.op(OP_SET, 'size', t);

			t = expect_token(ctx);
			switch (t[0]) {
			default:
				throw unexpected_token(ctx, t, 'a variable reference or a quoted string literal');

			case T_STRING:
			case T_VAR:
				world.op(OP_SET, 'content', t);
			}
			break;

		case T_STRING:
		case T_VAR:
			world.op(OP_SET, 'content', t);
		}
	}
	/* }}} */
	/* HTML block parser routine {{{ */
	var parse_html = function (world) {
		world.op(OP_START, 'html');
		parse_content(world, 'parsing an html content block');
		world.op(OP_END);
	};
	/* }}} */
	/* TEXT block parser routine {{{ */
	var parse_text = function (world) {
		world.op(OP_START, 'text');
		parse_content(world, 'parsing a text content block');
		world.op(OP_END);
	};
	/* }}} */
	/* THRESHOLD block parser routine {{{ */
	var parse_threshold = function(world) {
		var ctx = 'parsing a threshold name', t, rules = 0;

		t = expect_token(ctx);
		if (t[0] != T_STRING) { throw unexpected_token(ctx, t, 'a string value'); }

		ctx = 'parsing a threshold definition';
		expect_open(ctx);
		world.op(OP_START, 'threshold', t[1]);

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
				switch (val[0]) {
				case T_NUMERIC: case T_VAR: break;
				default: throw unexpected_token(ctx, val, 'a variable or a number');
				}

				var c = expect_token(ctx);
				if (c[0] != T_COLON) { throw unexpected_token(ctx, val, 'the rule separator, <tt>:</tt>'); }

				var col = expect_token(ctx);
				switch (col[0]) {
				case T_STRING: case T_IDENTIFIER: case T_VAR: break;
				default: throw unexpected_token(ctx, col, 'a color name, either as a variable reference, quoted string, or a bare word.');
				}

				switch (op[0]) {
				case T_GT: world.op(OP_TRULE, function (v,f) { return v >  f }, val, col); rules++; break;
				case T_GE: world.op(OP_TRULE, function (v,f) { return v >= f }, val, col); rules++; break;
				case T_LT: world.op(OP_TRULE, function (v,f) { return v <  f }, val, col); rules++; break;
				case T_LE: world.op(OP_TRULE, function (v,f) { return v <= f }, val, col); rules++; break;
				case T_EQ: world.op(OP_TRULE, function (v,f) { return v == f }, val, col); rules++; break;
				case T_NE: world.op(OP_TRULE, function (v,f) { return v != f }, val, col); rules++; break;
				}
				continue;
			}

			if (iskeyword(t, 'default')) {
				var c = expect_token(ctx);
				if (c[0] != T_COLON) { throw unexpected_token(ctx, val, 'the rule separator, <tt>:</tt>'); }

				var col = expect_token(ctx);
				switch (col[0]) {
				case T_STRING: case T_IDENTIFIER: case T_VAR: break;
				default: throw unexpected_token(ctx, col, 'a color name, either as a quoted string, or a bare word.');
				}

				world.op(OP_TRULE, function (v) { return true }, undefined, col);
				rules++;
				continue;
			}
			if (t[0] == T_CLOSE) {
				if (rules == 0) {
					throw "I couldn't make sense of your threshold;<br>"+
						  "You didn't specify any rules!";
				}
				world.op(OP_END);
				return;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or another threshold rule');
		}
	}
	/* }}} */
	/* METRIC block parser routine {{{ */
	var parse_metric = function(world) {
		var t, ctx = 'parsing a metric definition';
		expect_open(ctx);
		world.op(OP_START, 'metric');
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
			    iskeyword(t, 'unit')  ||
			    iskeyword(t, 'label') ||
			    iskeyword(t, 'graph') ||
			    iskeyword(t, 'query') ||
			    iskeyword(t, 'color')
			) {
				var val = expect_token(ctx);
				world.op(OP_SET, t[1], val);
				continue;
			}
			if (t[0] == T_CLOSE) {
				world.op(OP_END);
				//validate_size(ctx, o.size);
				//validate_query(ctx, o.query);
				return;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}
	/* }}} */
	/* SPARKLINE block parser routine {{{ */
	var parse_sparkline = function(world) {
		var t, ctx = 'parsing a sparkline definition';
		expect_open(ctx);
		world.op(OP_START, 'sparkline');
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
			    iskeyword(t, 'label') ||
			    iskeyword(t, 'color') ||
			    iskeyword(t, 'query') ||
			    iskeyword(t, 'plot')
			) {
				var val = expect_token(ctx);
				world.op(OP_SET, t[1], val);
				continue;
			}
			if (t[0] == T_CLOSE) {
				world.op(OP_END);
				//validate_size(ctx, o.size);
				//validate_query(ctx, o.query);
				return;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}
	/* }}} */
	/* GRAPH block parser routine {{{ */
	var parse_graph = function (world) {
		var t, ctx = 'parsing a graph definition';
		expect_open(ctx);
		world.op(OP_START, 'graph');
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
			    iskeyword(t, 'label') ||
			    iskeyword(t, 'query')
			) {
				var val = expect_token(ctx);
				world.op(OP_SET, t[1], val);
				continue;
			}
			if (iskeyword(t, 'x-axis') ||
			    iskeyword(t, 'y-axis') ||
			    iskeyword(t, 'axis')
			) {
				parse_axis(world, t[1]);
				continue;
			}
			if (iskeyword(t, 'plot')) {
				t = expect_token(ctx);
				var field = undefined;
				if (t[0] == T_STRING) {
					field = t;
					t = expect_token(ctx);
				}
				if (t[0] != T_OPEN) { throw unexpected_token(ctx, t, 'an opening curly brace, <tt>{</tt>'); }
				parse_plot(world, field);
				continue;
			}
			if (t[0] == T_CLOSE) {
				world.op(OP_END);
				//validate_size(ctx, o.size);
				//validate_query(ctx, o.query);
				return;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}
	/* }}} */
	/* SCATTERPLOT block parser routine {{{ */
	var parse_scatterplot = function (world) {
		var t, ctx = 'parsing a scatterplot definition';
		expect_open(ctx);
		world.op(OP_START, 'scatterplot');
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'size')  ||
			    iskeyword(t, 'label') ||
			    iskeyword(t, 'x') ||
			    iskeyword(t, 'y') ||
			    iskeyword(t, 'color') ||
			    iskeyword(t, 'query')
			) {
				var val = expect_token(ctx);
				world.op(OP_SET, t[1], val);
				continue;
			}
			if (iskeyword(t, 'x-axis') ||
			    iskeyword(t, 'y-axis') ||
			    iskeyword(t, 'axis')
			) {
				parse_axis(world, t[1]);
				continue;
			}
			if (t[0] == T_CLOSE) {
				world.op(OP_END);
				//validate_size(ctx, o.size);
				//validate_query(ctx, o.query);
				return;
			}
			throw unexpected_token(ctx, t, 'a closing curly brace, <tt>}</tt>, or a parameter keyword');
		}
	}
	/* }}} */
	/* PLACEHOLDER parser routine {{{ */
	var parse_placeholder = function(world) {
		var t, ctx = 'parsing a placeholder definition';
		expect_open(ctx);
		world.op(OP_START, 'placeholder');
		while ((t = expect_token(ctx)) !== undefined) {
			if (iskeyword(t, 'text')  ||
			    iskeyword(t, 'size')  ||
			    iskeyword(t, 'color')
			) {
				var val = expect_token(ctx);
				world.op(OP_SET, t[1], val);
				continue;
			}
			if (t[0] == T_CLOSE) {
				world.op(OP_END);
				//validate_size(ctx, o.size);
				return;
			}
			throw unexpected_token(ctx, t, 'either a closing brace, <tt>}</tt>, or a parameter keyword (i.e. text, size, color)');
		}
	}
	/* }}} */
	/* MAIN PARSER ROUTINE {{{ */
	var parse_toplevel = function (world, depth) {
		while (typeof(t = next()) !== 'undefined') {
			if (iskeyword(t, 'def')) {
				if (depth > 0) {
					throw with_lineno(
						'I found a nested function definition<br/>'+
						'But BoardCode doesn\'t currently supported functions outside the global scope');
				}
				parse_def(world, depth);
				continue;
			}
			if (iskeyword(t, 'let')) {
				parse_letvar(world);
				continue;
			}
			if (t[0] == T_VAR) {
				parse_setvar(world, t[1]);
				continue;
			}
			if (iskeyword(t, 'threshold')) {
				parse_threshold(world);
				continue;
			}
			if (iskeyword(t, 'html')) {
				parse_html(world);
				continue;
			}
			if (iskeyword(t, 'text')) {
				parse_text(world);
				continue;
			}
			if (iskeyword(t, 'metric')) {
				parse_metric(world);
				continue;
			}
			if (iskeyword(t, 'sparkline')) {
				parse_sparkline(world);
				continue;
			}
			if (iskeyword(t, 'break')) {
				world.op(OP_START, 'break');
				world.op(OP_END);
				continue;
			}
			if (iskeyword(t, 'graph')) {
				parse_graph(world);
				continue;
			}
			if (iskeyword(t, 'scatterplot')) {
				parse_scatterplot(world);
				continue;
			}
			if (iskeyword(t, 'placeholder')) {
				parse_placeholder(world);
				continue;
			}
			if (t[0] == T_IDENTIFIER) {
				if (t[1] in world.funs) {
					parse_call(world, t[1]);
					continue;
				}
				throw with_lineno("Uh-oh.  I don't recognize '"+t[1]+"' as either a block type or a defined function");
			}
			if (depth > 0 && t[0] == T_CLOSE) {
				return;
			}
			throw unexpected_token('parsing your board', t, "a block keyword (like '<tt>metric</tt>' or '<tt>graph</tt>')");
		}
	}
	/* }}} */

	var world = new World();
	parse_toplevel(world, 0);
	console.log("returning %s", world);
	return world;
};

var evaluate = function (world) {
	return world.eval('main');
};

var Board = (function () {
	var ID = 0;
	return function (link, name, code) {
		if (arguments.length == 1) {
			code = link;
			link = name = undefined;
		}

		this.code = code;
		this.blocks = evaluate(parse(code));
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
	this.blocks = evaluate(parse(code));

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
