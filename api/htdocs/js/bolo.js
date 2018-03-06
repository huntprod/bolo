(function (document,window,undefined) {
	var init = false;
	var c1, picker, w1 = 200, h1 = 200;
	var c2, strip,  w2 =  16, h2 = 200;
	var swatches;

	var hex = function (d) {
		var x = '0123456789abcdef';
		return '#' + x[(d[0] & 0xf0) >> 4] + x[(d[0] & 0x0f)] +
		             x[(d[1] & 0xf0) >> 4] + x[(d[1] & 0x0f)] +
		             x[(d[2] & 0xf0) >> 4] + x[(d[2] & 0x0f)];
	};

	var gradient = function (base) {
		var g;

		c1.fillStyle = base;
		c1.fillRect(0, 0, w1, h1);

		g = c1.createLinearGradient(0, 0, w1, 0);
		g.addColorStop(0, 'rgba(255,255,255,1)');
		g.addColorStop(1, 'rgba(255,255,255,0)');
		c1.fillStyle = g;
		c1.fillRect(0, 0, w1, h1);

		g = c1.createLinearGradient(0, h1, 0, 0);
		g.addColorStop(0, 'rgba(0,0,0,1)');
		g.addColorStop(1, 'rgba(0,0,0,0)');
		c1.fillStyle = g;
		c1.fillRect(0, 0, w1, h1);
	};

	var swatch = function (color) {
		swatches.append(
			$('<li>')
			  .css({backgroundColor: color})
			  .attr('data-color', color));

		while (swatches.find('li').length > 40) {
			swatches.find('li').first().remove();
		}

		return color;
	};

	var choose = function (color) {
		$('textarea[name=code]').write(color);
		$('#colorpicker').hide();
	};

	$.fn.colorpicker = function () {
		if (!init) {
			$(document.body).append($('<div id="colorpicker">'+
			                            '<ul class="swatches"></ul>'+
			                            '<canvas class="picker"></canvas>'+
			                            '<canvas class="strip"></canvas>'+
			                          '</div>').hide());
			picker   = $('#colorpicker .picker').attr({height: h1, width: w1});
			strip    = $('#colorpicker .strip').attr({height: h2, width: w2});
			swatches = $('#colorpicker .swatches');

			/* initialize swatch palette */
			swatches.on('click', 'li', function (event) {
				event.preventDefault();
				event.stopPropagation();

				var color = $(event.target).data('color');
				choose(color);
			});

			/* initialize color saturation/value picker */
			c1 = picker[0].getContext('2d');
			gradient('rgba(255,0,0,1)');

			picker.on('click', function (event) {
				event.preventDefault();
				event.stopPropagation();

				var x = event.offsetX,
				    y = event.offsetY,
				    d = c1.getImageData(x,y, 1,1).data;

				choose(swatch(hex(d)));
			});


			/* initialize color hue picker */
			c2 = strip[0].getContext('2d');
			var g = c2.createLinearGradient(0, 0, 0, h2);
			g.addColorStop(0.00, 'rgba(255,   0,   0, 1)');
			g.addColorStop(0.17, 'rgba(255, 255,   0, 1)');
			g.addColorStop(0.34, 'rgba(  0, 255,   0, 1)');
			g.addColorStop(0.51, 'rgba(  0, 255, 255, 1)');
			g.addColorStop(0.68, 'rgba(  0,   0, 255, 1)');
			g.addColorStop(0.85, 'rgba(255,   0, 255, 1)');
			g.addColorStop(1.00, 'rgba(255,   0,   0, 1)');
			c2.fillStyle = g;
			c2.fillRect(0, 0, w2, h2);

			strip.on('click', function (event) {
				event.preventDefault();
				event.stopPropagation();

				var x = event.offsetX,
				    y = event.offsetY,
				    d = c2.getImageData(x,y, 1,1).data;

				gradient('rgba(' + d[0] + ',' + d[1] + ',' + d[2] + ',1)');
			});

			swatch('#004c99'); /* blue */
			swatch('#990000'); /* red */
			swatch('#00994c'); /* green */
			swatch('#cccc00'); /* yellow */
			swatch('#ff8000'); /* orange */
			swatch('#00cccc'); /* cyan */
			swatch('#6600cc'); /* purple */
			swatch('#cc0066'); /* pink */
			swatch('#808080'); /* grey */
		}

		this.on('click', function (event) {
			event.preventDefault();
			event.stopPropagation();

			var $ctl = $(event.target),
			    offt = $ctl.offset();

			$('#colorpicker').show().css({
				top  :     offt.top,
				left : 5 + offt.left + $ctl.width()
			});
		});
	};
})(document,window);
(function (document,window,undefined) {
	var $BOARDS = [];
	var $AUTH = undefined;

	window.error_from = function (s) {
		try {
			res = JSON.parse(s)
			return res.error;
		} catch (oops) {
			return s;
		}
	}

	$.fn.message = function (css, message) {
		page(this.attr('id'));
		this.empty().append('<div class="w12 ' + css + '">' + message + '</div>');
	}
	$.fn.error = function (message, error) {
		this.message('error',
			'<p>' + message +
				'<span>' + error.toString() + '</span>' +
			'</p>');
	}

	$.fn.write = function (s) {
		var e = this[0];
		if (document.selection) {
			e.focus();
			var ss = document.selection.createRange();
			ss.text = s;

		} else if (e.selectionStart || e.SelectionStart == '0') {
			var a = e.selectionStart;
			    b = e.selectionEnd;

			e.value = e.value.substr(0, a) + s + e.value.substring(b, e.value.length);
			b = a + s.length;
			e.setSelectionRange(b,b);
			e.focus();

		} else {
			e.value += s;
			e.focus();
		}
	};

	function editor(board) {
		var $e = $('#editor');
		$e.find('.fail, .preview').hide();

		$e.attr('method', board.link ? 'PUT' : 'POST');
		$e.find('input[name=link]').val(board.link || '');
		$e.find('input[name=name]').val(board.name);
		$e.find('textarea').val(board.code);
		if (board.new) {
			$e.attr('method', 'POST');
			$e.find('input[name=link]').attr('type', 'text').data('changed', 'no');
		} else {
			$e.attr('method', 'PUT');
			$e.find('input[name=link]').attr('type', 'hidden').data('changed', 'yes');
		}

		page('editor');
		$e.find('textarea').focus();
	}

	function current() {
		var href = $('#top-nav .active a').attr("href");
		for (var i = 0; i < $BOARDS.length; i++) {
			if ($BOARDS[i].href == href) { return $BOARDS[i]; }
		}
		return undefined;
	}

	function page(name) {
		$('body').attr('page', name);
		$('html, body').animate({ scrollTop: 0 }, 400);
	}

	function activate(a) {
		if (typeof(a) === 'string') {
			a = $('#top-nav a[href="' + (a.substr(0,1) == '#' ? a : '#'+a)+'"]');;
		}
		if ($(a).length) {
			$('#top-nav li.active').removeClass('active');
			$(a).closest('li').addClass('active');
		}
	}

	var navbar = function(want) {
		if (!$AUTH) { return; }
		var cur = current();
		$('#top-nav li.board').remove();
		var adder = $('#top-nav li.action').detach();

		for (var i = 0; i < $BOARDS.length; i++) {
			var classes = 'board';
			if (cur ? cur.id == $BOARDS[i].id : i == 0) { classes += ' active'; }
			if ($BOARDS[i].deleted)                             { classes += ' deleted'; }
			$('#top-nav').append(
				$('<li class="' + classes + '">' +
				     '<a href="' + $BOARDS[i].href + '">' + $BOARDS[i].name + '</a>' +
				  '</li>')
			);
		}
		$('#top-nav').append(adder);
		activate(want);
	}

	var NODATA = undefined;
	function api(request, data, options) {
		p = request.split(' ');
		if (data) { options.data = JSON.stringify(data) }
		$.ajax($.extend({
			type: p[0],
			url:  p[1],

			contentType: 'application/json; charset=utf8',
			dataType:    'json',
			processData: false,
		}, options));
	}

	function refresh() {
		if (!$AUTH) { return; }
		var cur = current();
		if (!cur) {
			if ($('body').attr('access') == 'write') {
				$('#oops').message('banner admin-setup',
					'<h2>Welcome to Bolo!</h2>'+
					'<p>You don\'t have any boards defined yet.'+
					   'Boards visualize your monitoring data for you, by drawing graphs, '+
					   'showing trend lines, displaying metric values, and more.</p>'+
					'<button rel="add">Create Your <span>First</span> Dashboard</button>'
				);
			} else {
				$('#oops').message('banner',
					'<h2>Welcome to Bolo!</h2>'+
					'<p>You don\'t have any boards defined yet.</p><p>'+
					   'Please ask your Bolo administrator to configure a few boards, '+
					   'so that you can see the data in your monitoring system.</p>'
				);
			}
			return;
		}

		try {
			cur.draw($('#board'));
		} catch (e) {
			$('#board').error("Uh-oh, There's something wrong with this board...", e);
			return;
		}
	}

	if ($(document.body).is('.app')) {
		/* APPLICATION setup routines {{{ */
		$(function () {
			api('GET /v1/auth', null, {
				success: function (auth) {
					$AUTH = auth;
					Board.lookup(function (boards, FIXME) {
						$BOARDS = boards;
						$('body').attr('data-access', $AUTH.access).removeClass('preauth');
						if ($AUTH.access != 'write') {
							$('a[rel=add], a[rel=edit], a[rel=delete]').remove();
						}

						navbar(document.location.hash);
						page('board');
						refresh();
					});
				},
				error: function (r) {
					if (r.status == 401) {
						page('login');
						$('#login .login [name=username]').focus();
					}
				}
			});

			$('#editor #insert-color').colorpicker();

			$(document.body)
			.on('click', function (event) {
				$('#colorpicker').hide();
			})
			.on('keyup', function (event) {
				if (event.keyCode == 27) { /* ESC */
					$('#colorpicker').hide();
				}
			})
			.on('click', '#editor #insert-graph', function (event) {
				$('textarea[name=code]').write(
					'\ngraph {\n'+
					'  size 4x3\n'+
					'  query [...]\n'+
					'\n'+
					'  plot "..." {\n'+
					'    as area\n'+
					'    color skyblue\n'+
					'  }\n'+
					'}\n\n');
			})
			.on('click', '#editor #insert-sparkline', function (event) {
				$('textarea[name=code]').write(
					'\nsparkline {\n'+
					'  label  "My Sparkline"\n'+
					'  query  [...]\n'+
					'}\n\n');
			})
			.on('click', '#editor #insert-scatterplot', function (event) {
				$('textarea[name=code]').write(
					'\nscatterplot {\n'+
					'  size 4x3\n'+
					'  query [...]\n'+
					'  x used\n'+
					'  y total\n'+
					'}\n\n');
			})
			.on('click', '#editor #insert-placeholder', function (event) {
				$('textarea[name=code]').write(
					'placeholder {\n'+
					'  size 3x3\n'+
					'  text "placeholder"\n'+
					'}\n\n');
			})
			.on('click', '#editor button[type=preview]', function (event) {
				event.preventDefault();
				$('#editor .fail').hide();

				try {
					new Board($('#editor textarea[name=code]').val())
					         .draw($('#editor .preview'));
					$('#editor .preview').show();
					$('html, body').animate({
						scrollTop: $("#editor .preview").offset().top
					}, 1400);

				} catch (e) {
					console.log('BOLO ERROR (caught): ', e);
					$('#editor .fail').empty().append(e).show();
					return;
				}
			})
			.on('change', '#editor [name=link]', function (event) {
				var $link = $(event.target);
				$link.data('changed', $link.val() == '' ? 'no' : 'yes');
			})
			.on('keyup', '#editor [name=name]', function (event) {
				var $name = $(event.target);
				var $link = $('#editor [name=link]');
				if ($link.data('changed') == 'no') {
					$link.val($name.val().toLowerCase().replace(/[^a-z]+/g, '-').replace(/(^-)|(-$)/g, ''));
				}
			})
			.on('submit', '#login', function (event) {
				event.preventDefault();
				$('#login .error').hide();

				var data = {
					username: $('#login input[name=username]').val(),
					password: $('#login input[name=password]').val()
				};

				api('POST /v1/auth', data, {
					success: function () {
						document.location.href = '/';
					},
					error: function (r) {
						$('#login .error').html(r.responseJSON.error).show();
					}
				});
			})
			.on('click', 'a[rel=logout]', function (event) {
				event.preventDefault();

				api('DELETE /v1/auth', null, {
					complete: function () {
						document.location.href = '/';
					}
				});
			})
			.on('submit', '#editor', function (event) {
				event.preventDefault();

				var data = {
					link: $('#editor input[name=link]').val(),
					name: $('#editor input[name=name]').val(),
					code: $('#editor textarea[name=code]').val(),
				};

				/* check syntax before we submit */
				try {
					new Board('', 'preview', data.code);
				} catch (e) {
					$('#editor .fail').empty().append(e).show();
					return;
				}

				var action = 'POST /v1/boards';
				if (data.link != "" && $(event.target).attr('method') == 'PUT') {
					action = 'PUT /v1/boards/'+data.link;
				}

				api(action, data, {
					success: function (data) {
						var cur = current();
						if (!cur) {
							$BOARDS.push(new Board(
								data.link,
								data.name,
								data.code
							));
							navbar(data.link);

						} else {
							cur.update(
								data.link,
								data.name,
								data.code
							);
						}
						page('board');
						refresh();
					},

					error: function (r) {
						$('#editor .fail').empty().append(
							'Unable to save your board:<br>'+
							error_from(r.responseText)).show();
					}
				});
			})
			.on('click', '[rel=add]', function (event) {
				event.preventDefault();
				activate(event.target);
				editor({
					new:  true,
					name: 'My New Board',
					link: 'my-new-board',
					code: "%\n% My New Board\n%\n\n"
				});
			})
			.on('click', '[rel=edit]', function (event) {
				event.preventDefault();
				editor(current());
			})
			.on('click', '[rel=delete]', function (event) {
				event.preventDefault();
				var cur = current();
				if (!confirm("Are you sure you want to delete '"+cur.name+"'?")) {
					return;
				}

				api('DELETE /v1/boards/'+cur.link, NODATA, {
					success: function () {
						cur.deleted = true;
						$('#board').addClass('deleted');
						navbar();
					}
				});
			})
			.on('click', '[rel=undo]', function (event) {
				event.preventDefault();
				api('POST /v1/boards', current(), {
					success: function (data) {
						current().deleted = false;
						navbar();
						refresh();
					}
				});
			})
			.on('click', '#top-nav li.board a', function (event) {
				page('board');
				activate(event.target);
				refresh();
			})
			.on('click', 'a[rel=explore]', function (event) {
				page('explore');
			})
			.on('submit', '#explore', function (event) {
				event.preventDefault();
				var q = $(event.target).closest('form').find('[name=q]').val();
				api("POST /v1/plan", {"q":q}, {
					error: function (r) {
						$('#oops').message('error',
							'Unable to render the exploratory data dashboard: '+
							r.responseJSON.error
						);
					},
					success: function (data) {
						var text = [];

						// check for things we can't handle
						for (var i = 0; i < data.select.length; i++) {
							if (data.select[i].fn) {
								$('#oops').message('error',
									'Bolo does not yet support exploratory data analysis '+
									'using consolidation function (like '+data.select[i].fn+')'
								);
								return;
							}
						}

						// create at-a-glance sparklines
						/*
						text.push([
							'placeholder {',
							'  size 12x1',
							'  text ['+q+']',
							'  color white/navy',
							'}'
						].join('\n'));
						*/

						for (var i = 0; i < data.select.length; i++) {
							var field = data.select[i];
							text.push([
								'sparkline {',
								'  size 12x1',
								'  label "'+field+'"',
								'  query ['+q+']',
								'  plot "'+field+'"',
								'}'
							].join('\n'));
						}
						text.push('break');

						// create x-vs-y scatter plots
						for (var i = 0; i < data.select.length; i++) {
							var x = data.select[i];
							for (var j = i+1; j < data.select.length; j++) {
								var y = data.select[j].name;
								text.push([
									'% scatter plot, '+x+' vs '+y,
									'scatterplot {',
									'  size 4x3',
									'  query ['+q+']',
									'  x "'+x+'"',
									'  y "'+y+'"',
									'}',
								].join('\n'));
							}
						}
						text.push('break');

						// create the "simple" plot graphs
						for (var i = 0; i < data.select.length; i++) {
							var field = data.select[i];
							text.push([
								'graph {',
								'  size 12x3',
								'  label "'+field+'"',
								'  query ['+q+']',
								'  plot "'+field+'" {',
								'    as line',
								'  }',
								'}'
							].join('\n'));
						}
						text.push('break');

						if (data.select.length > 1) {
							// create the "combined" plot graph
							text.push([
								'graph {',
								'  size 12x6',
								'  label "composite"',
								'  query ['+q+']'].join('\n'));
							var color = 'red bue green orange'.split(" ");
							for (var i = 0; i < data.select.length; i++) {
								var field = data.select[i];
								text.push([
									'  plot "'+field+'" {',
									'    as line',
									'    color '+color[i % color.length],
									'  }',
								].join('\n'));
							}
							text.push('}');
							text.push('break');
						}

						try {
							var board = new Board(text.join('\n\n'));
							board.draw($('#explore .board').empty());
						} catch (e) {
							console.log('FAILED to render board!');
							console.log('source was:');
							console.log(text.join('\n\n'));
							console.log('error was ', e);

							$('#oops').message('error',
								'<p>There was an error rendering the exploratory dashboard.  '+
								'Please file a bug.</p>'
							);
						}
					}
				});
			})
			.on('click', 'a[rel=browse]', function (event) {
				api('GET /v1/series', NODATA, {
					success: function (data) {
						page('browse');
						$('#browse').data('results', data)
						            .trigger('submit');
					}
				});
			})
			.on('keyup', '#browse [name=q]', function (event) {
				$(event.target).closest('form').trigger('submit');
			})
			.on('submit', '#browse', function (event) {
				event.preventDefault();
				$('#browse .results').empty().append($(
					'<table>'+
					  '<thead><tr><th>Series</th><th>Metric</th></tr></thead>'+
					  '<tbody></tbody>'+
					  '<tfoot><tr><th>Series</th><th>Metric</th></tr></tfoot>'+
					'</table>'));
				$('#browse .results tfoot').hide();
				var $t = $('#browse .results table tbody');

				var ll = [];
				var data = $('#browse').data('results');
				var q = new RegExp($(event.target).find('input').val());
				try { "".match(q) } catch (e) { q = undefined }
				for (var series in data) {
					for (var i = 0; i < data[series].length; i++) {
						if (!q || series.match(q) || data[series][i].match(q)) {
							ll.push([series+' '+data[series][i], series, data[series][i]]);
						}
					}
				}
				ll.sort(function (a, b) { return a[0] < b[0] ? -1
				                               : a[0] > b[0] ?  1 : 0 });

				var last = undefined;
				var n = 0;
				for (var i = 0; i < ll.length; i++) {
					var css = '';
					if (typeof(last) == 'undefined' || last != ll[i][1]) {
						n++;
						css = 'first ';
						last = ll[i][1];
					}
					if (i+1 < ll.length && ll[i][1] != ll[i+1][1]) {
						css += 'last ';
					}
					css += 'r'+(n % 2);
					var series = q ? ll[i][1].replace(q, '<span class="m">$&</span>') : ll[i][1];
					var metric = q ? ll[i][2].replace(q, '<span class="m">$&</span>') : ll[i][2];
					$t.append($('<tr class="'+css+'"'+
					              ' series="'+ll[i][1]+'" metric="'+ll[i][2]+'">'+
					              '<td class="data-series">'+series+'</td>'+
					              '<td class="data-metric">'+metric+'</td>'+
					            '</tr>'));
				}
				if (ll.length > 20) {
					$('#browse .results tfoot').show();
				}
			})
			.on('click', '#browse .data-series', function (event) {
				var series = $(event.target).closest('tr').attr('series');
				var f = $('#browse').data('results')[series];
				if (!f.length) {
					console.log('no fields found for '+series);
					return;
				}

				page('explore');
				$('#explore [name=q]').val("SELECT "+f.join(', ')+" FROM "+series+" AFTER 4h AGO");
				$('#explore').trigger('submit');
			})
			.on('click', '#browse .data-metric', function (event) {
				var metric = $(event.target).closest('tr').attr('metric');
				var series = $(event.target).closest('tr').attr('series');
				page('explore');
				$('#explore [name=q]').val("SELECT "+metric+" FROM "+series+" AFTER 4h AGO");
				$('#explore').trigger('submit');
			})
			.on('click', '.hover .grip', function (event) {
				event.preventDefault();
				$(event.target).closest('.hover').find('.pop').toggle();
			})
			;
		});
		/* }}} */
	} else if ($(document.body).is('.docs')) {
		/* DOCUMENTATION setup routines {{{ */
		var random = function (n, options) {
			var between   = function (a, b)    { return a+Math.random()*(b-a); }
			var spread    = function (spread)  { return between(spread / -2, spread / 2); }
			var constrain = function (x, a, b) { return Math.min(Math.max(x, a), b); }

			var datum = between(options.min, options.max);
			var data = [];
			var now = new Date().getTime() / 1000;
			for (var i = 0; i < n; i++) {
				datum = constrain(datum + spread(options.spread), options.min, options.max);
				data.push({t:now - (n - i)*7200, v:datum});
			}
			return data;
		};
		var data = { metric: random(700, {min:10, max:240*0.25, spread:4}),
		             other:  random(700, {min:10, max:240*0.25, spread:4}) };

		$('.example').each(function (i, ex) {
			var board, block;
			try {
				board = new Board($(ex).text());
			} catch (e) {
				$(ex).addClass('error').html(e.toString());
				return;
			}

			block = board.blocks[0];
			try {
				$(ex).html(block.html());
				block.update(data);
			} catch (e) {
				if (e.toString().match(/more than one metric/i)) {
					try {
						block.update({metric: data.metric});
					} catch (e) {
						$(ex).addClass('error').html("Unable to render example BoardCode: " + e.toString());
						return;
					}
				} else {
					$(ex).addClass('error').html("Unable to render example BoardCode: " + e.toString());
					return;
				}
			}
		});
		$('.full-example').each(function (i, ex) {
			var board;
			try {
				if ($(ex).is('[data-from]')) {
					board = new Board($('[data-example="'+$(ex).attr('data-from')+'"]').text());
				} else {
					board = new Board($(ex).text());
				}
			} catch (e) {
				throw e;
				$(ex).addClass('error').html("Unable to parse example BoardCode: "+e.toString());
				return;
			}

			$(ex).empty();
			for (var i = 0; i < board.blocks.length; i++) {
				$(ex).append(board.blocks[i].html());
				try {
					board.blocks[i].update(data);
				} catch (e) {
					if (e.toString().match(/more than one metric/i)) {
						try {
							board.blocks[i].update({metric: data.metric});
						} catch (e) {
							$(ex).addClass('error').html("Unable to render example BoardCode: " + e.toString());
							return;
						}
					} else {
						$(ex).addClass('error').html("Unable to render example BoardCode: " + e.toString());
						return;
					}
				}
			}
		});

		$toc = $('.docs #toc');
		$('.docs h2[toc]').each(function (i, h2) {
			var $h2 = $(h2);
			var $a = $h2.prev();
			var title = $h2.attr('toc');
			if (title == '*') { title = $h2.html(); }

			$toc.append('<li><a href="#'+$a.attr('id')+'">'+title+'</a></li>');
		});
		/* }}} */
	}
})(document,window);
