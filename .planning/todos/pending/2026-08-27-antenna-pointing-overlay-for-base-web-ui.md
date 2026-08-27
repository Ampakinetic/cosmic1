---
created: 2026-08-27T11:23:49Z
title: Antenna-pointing overlay for base web UI
area: ui
severity: minor
files:
  - src/main_basestation.cpp
---

## Problem

The base web page will be viewed on an Android tablet in Chrome, mounted on a tripod
fixed to the Yagi antenna on the same plane — tablet orientation equals antenna
orientation. The operator currently has no aiming aid and must judge antenna pointing
by manual compass + map. Wanted: an on-screen feature showing the compass heading and
elevation angle to point the Yagi at the balloon, with an arrow indicating where the
balloon is RELATIVE to the tablet's current orientation, using the tablet's own
sensors to resolve that relative position.

User-confirmed routing (2026-08-27): new roadmap phase (separate from thumb-first
delivery), planned AFTER bench session #6 / plan 01-20 completes.

## Solution

Web UI overlay using DeviceOrientationEvent (Android Chrome: sensor access requires a
user gesture to grant — DeviceOrientationEvent.absolute / webkitCompassHeading for
true-north heading; flag the HTTPS-or-localhost requirement for the permission API,
relevant since the base serves over plain HTTP on its AP). Geometry: great-circle
bearing + elevation angle from base position to balloon GPS telemetry (lat/lon/alt
already flowing; /api/state carries the track array). Base station position has no
GPS — needs manual config entry in the web UI (persisted browser-side or served from
a settings endpoint; decide in planning). Arrow = bearing-to-balloon minus tablet
compass heading (azimuth rotation), plus elevation indicator from the tilt (beta)
once the tablet plane is the antenna plane. Edge cases for planning: stale/no GPS
fix, magnetometer calibration drift, missing sensor permission fallback (show
numeric heading/angle without the live arrow).
