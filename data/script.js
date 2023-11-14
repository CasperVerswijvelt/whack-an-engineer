const wsUri = `ws://${window.location.hostname}/ws`;
// const wsUri = `ws://192.168.69.152/ws`;

const GAME_IDLE = 1;
const GAME_STARTING = 2;
const GAME_PLAYING = 3;
const GAME_END = 4;

const gameStateClassMap = {
    [GAME_IDLE]: "game--idle",
    [GAME_STARTING]: "game--starting",
    [GAME_PLAYING]: "game--playing",
    [GAME_END]: "game--end",
}

const formatSeconds = (seconds) => {
    const date = new Date(0);
    date.setSeconds(seconds);
    // get mm:ss part of date string
    return date.toISOString().substring(14, 19);
}

const initWebSocket = () => {
    console.log('Trying to open a WebSocket connection...');
    const websocket = new WebSocket(wsUri);
    websocket.onopen = () => console.log("Connection opened");
    websocket.onclose = () => {
        console.log('Connection closed');
        document.getElementById("game").className = "loading"
        setTimeout(initWebSocket, 2000);
    };
    websocket.onmessage = (evt) => {
        const split = evt.data.split(" ");
        const id = split[0];
        const value = split[1];

        switch (id) {
            case "gameState":
                document.getElementById("game").className = gameStateClassMap[value]
                break;
            case "score":
                document.getElementById("score").innerHTML = value
            case "time":
                document.getElementById("clock").innerHTML = formatSeconds(parseInt(value))
        }
    };
}

window.addEventListener('load', initWebSocket);