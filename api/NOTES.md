Bolo Web UI Notes
=================

z-indices are important, since stacking them incorrectly may lead
to weird behavior that you won't see until someone else shows it
to you.  In other words, you can't see all the screens, so stick
to these principles:

First, most things are on z-index: 0.  This includes `<body>`, and
all of it's descendants.  This is the bottom of the page stack.

As you will recall, CSS counts z-index towards the viewer, so a
higher z-index is in front of a lower z-index.

The rest of the z-index strategy looks like this:

    103: modal window header + chrome
    102: modal window field
    101: modal overlay

     53: board code block hover text
     52: board code per-block widgets
     51: board code blocks
     50: board code boards
