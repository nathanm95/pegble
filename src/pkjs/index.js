// Phone side of src/c/pins.c: turns the watch's numbers into a timeline pin.
//
// The current Pebble app keeps pins locally and offers Pebble.insertTimelinePin
// for that. Older apps (and Rebble's) only know the web API; the newer app also
// intercepts calls to it and turns them into local pins, so that's the fallback.

var PIN_DAILY = 1;
var PIN_ENDLESS = 2;

function withCommas(n) {
  return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

function buildPin(kind, score, a, b) {
  var now = new Date().toISOString();
  if (kind === PIN_DAILY) {
    return {
      // One pin per day: a better score later the same day updates it. Pin ids
      // keep the game's old name (Pegble) so existing pins update, not duplicate.
      id: 'pegble-daily-' + a,
      time: now,
      layout: {
        type: 'genericPin',
        title: 'Daily: ' + withCommas(score),
        subtitle: 'Peggle',
        body: (b > 1 ? b + ' days in a row. ' : '') + 'A new board tomorrow.',
        tinyIcon: 'system://images/NOTIFICATION_FLAG'
      }
    };
  }
  return {
    id: 'pegble-endless-record',
    time: now,
    layout: {
      type: 'genericPin',
      title: 'Endless record: ' + a + (a === 1 ? ' board' : ' boards'),
      subtitle: 'Peggle',
      body: 'New best run, scoring ' + withCommas(score) + '.',
      tinyIcon: 'system://images/NOTIFICATION_FLAG'
    }
  };
}

function insertPin(pin) {
  if (typeof Pebble.insertTimelinePin === 'function') {
    Pebble.insertTimelinePin(pin);
    return;
  }
  Pebble.getTimelineToken(function (token) {
    var req = new XMLHttpRequest();
    req.open('PUT', 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id);
    req.setRequestHeader('Content-Type', 'application/json');
    req.setRequestHeader('X-User-Token', token);
    req.send(JSON.stringify(pin));
  }, function () {
    // No token: no timeline on this phone. Nothing to do.
  });
}

Pebble.addEventListener('appmessage', function (e) {
  var d = e.payload || {};
  var kind = d.PinKind;
  if (kind !== PIN_DAILY && kind !== PIN_ENDLESS) {
    return;
  }
  insertPin(buildPin(kind, d.PinScore || 0, d.PinA || 0, d.PinB || 0));
});
