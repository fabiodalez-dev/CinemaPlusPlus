/*
 * Cinema screen saver.
 *
 * Cinema's own: the Home wallpapers, one fading into the next every twenty
 * seconds while it pushes slowly in, with the time and the date in a corner
 * that moves every minute. The pictures are copied beside this file when the
 * screen saver is staged (__TVWEB_WALLS__), so the platform's runner reads its
 * own folder only. Mounted over com.webos.app.screensaver like the others.
 */
import QtQuick 2.4
import Eos.Window 0.1

WebOSWindow {
    id: win

    width: 1920
    height: 1080

    windowType: "_WEBOS_WINDOW_TYPE_SCREENSAVER"
    appId: "com.webos.app.screensaver"
    title: "Screen Saver"
    visible: true
    color: "black"

    property int level: __TVWEB_LEVEL__
    property var walls: __TVWEB_WALLS__
    property var days: __TVWEB_DAYS__
    property var months: __TVWEB_MONTHS__
    property int index: 0
    property bool front: true

    FontLoader { id: lgLight; source: "file:///usr/share/fonts/LG_Display-Light.ttf" }
    FontLoader { id: lgRegular; source: "file:///usr/share/fonts/LG_Display-Regular.ttf" }

    function wall(i) { return walls.length ? Qt.resolvedUrl(walls[i % walls.length]) : "" }

    Image {
        id: one
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        source: win.wall(0)
        opacity: win.front ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 2400; easing.type: Easing.InOutQuad } }
        NumberAnimation on scale { id: pushOne; from: 1.0; to: 1.08; duration: 24000; running: true }
    }
    Image {
        id: two
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        opacity: win.front ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 2400; easing.type: Easing.InOutQuad } }
        NumberAnimation on scale { id: pushTwo; from: 1.0; to: 1.08; duration: 24000; running: false }
    }

    // Darker when dim: a screen saver is on for hours and is there to spare the panel.
    Rectangle { anchors.fill: parent; color: "black"; opacity: win.level > 0 ? 0.25 : 0.5 }

    Timer {
        interval: 20000; repeat: true; running: win.walls.length > 1
        onTriggered: {
            win.index = (win.index + 1) % win.walls.length;
            if (win.front) { two.source = win.wall(win.index); pushTwo.restart() }
            else { one.source = win.wall(win.index); pushOne.restart() }
            win.front = !win.front;
        }
    }

    function two0(n) { return n < 10 ? "0" + n : "" + n }

    // Which corner the time is in: bindings, so the place follows the text's real size.
    property bool placeLeft: true
    property bool placeTop: false

    Column {
        id: corner
        x: win.placeLeft ? 96 : 1920 - width - 96
        y: win.placeTop ? 88 : 1080 - height - 88
        Behavior on x { NumberAnimation { duration: 2600; easing.type: Easing.InOutQuad } }
        Behavior on y { NumberAnimation { duration: 2600; easing.type: Easing.InOutQuad } }
        Text {
            id: time
            font.family: lgLight.status === FontLoader.Ready ? lgLight.name : lgRegular.name
            font.pixelSize: 150
            color: "#ffffff"
            opacity: win.level > 0 ? 0.95 : 0.75
        }
        Text {
            id: date
            font.family: lgRegular.name
            font.pixelSize: 42
            color: "#ffffff"
            opacity: win.level > 0 ? 0.85 : 0.6
        }
    }

    property int minuteSeen: -1
    Timer {
        interval: 1000; repeat: true; running: true; triggeredOnStart: true
        onTriggered: {
            var now = new Date();
            time.text = win.two0(now.getHours()) + ":" + win.two0(now.getMinutes());
            date.text = win.days[now.getDay()] + " " + now.getDate() + " " + win.months[now.getMonth()];
            // A new place every minute, among the four corners' neighbourhoods.
            if (now.getMinutes() !== win.minuteSeen) {
                win.minuteSeen = now.getMinutes();
                win.placeLeft = (now.getMinutes() % 2) === 0;
                win.placeTop = (now.getMinutes() % 4) >= 2;
            }
        }
    }
}
