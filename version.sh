#!/bin/sh

major=$(awk '/^#define BOLO_VERSION_MAJOR / { print $3 }' < bolo.h)
minor=$(awk '/^#define BOLO_VERSION_MINOR / { print $3 }' < bolo.h)
point=$(awk '/^#define BOLO_VERSION_POINT / { print $3 }' < bolo.h)
echo $major.$minor.$point
