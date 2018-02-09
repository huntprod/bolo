QUnit.module("BoardCode blocks");
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
QUnit.test("parse errors", function (Ω) {
  Ω.throws(function () {
    new Board($('#unknown-block-1').text());
  }, "the parser should catch unknown blocks");
});
