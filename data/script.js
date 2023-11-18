// const wsUri = `ws://${window.location.hostname}/ws`;
const wsUri = `ws://192.168.69.152/ws`;

const GAME_IDLE = 1;
const GAME_STARTING = 2;
const GAME_PLAYING = 3;
const GAME_END = 4;

const K_SCORES = "scores"

let webSocket
let wsPongTimeout;
let currentGameState = 0;
let lastScore = 0;

const gameStateClassMap = {
    [GAME_IDLE]: "game--idle",
    [GAME_STARTING]: "game--starting",
    [GAME_PLAYING]: "game--playing",
    [GAME_END]: "game--end",
}

const getScores = () => {
    const rawScores = localStorage.getItem(K_SCORES);
    try {
        const parsed = JSON.parse(rawScores);
        if (Array.isArray(parsed)) {
            return parsed.sort((a, b) => {
                return (
                    a.score > b.score ||
                    (
                        a.score === b.score &&
                        a.timestamp < b.timestamp
                    )
                );
            })
        };
    } catch (e) { }
    return [];
}


const addScore = (name, score) => {
    const scores = getScores();
    const finalName = name.slice(0, 3);

    scores.push({
        name: finalName,
        score: score,
        timestamp: Date.now()
    })

    localStorage.setItem(
        K_SCORES,
        JSON.stringify(scores)
    );
}

const nth = (d) => {
    if (d > 3 && d < 21) return 'th';
    switch (d % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
};

const updateScoreBoard = () => {
    const tableBody = document.getElementById("scoreboard-table-body");
    const scores = getScores();

    tableBody.innerHTML = "";

    const entryCount = 20;

    for (let i = 0; i < entryCount; i++) {
        const score = scores[i] ?? {
            name: "---",
            score: "-",
        }

        const trNode = document.createElement("tr");
        const rankNode = document.createElement("th");
        rankNode.innerText = `${i + 1}${nth(i + 1).toUpperCase()}`;
        const nameNode = document.createElement("th");
        nameNode.innerText = score.name;
        const scoreNode = document.createElement("th");
        scoreNode.innerText = score.score;
        trNode.appendChild(rankNode);
        trNode.appendChild(nameNode);
        trNode.appendChild(scoreNode);
        tableBody.appendChild(trNode);
    }
}

const formatSeconds = (seconds) => {
    const date = new Date(0);
    date.setSeconds(seconds);
    // get mm:ss part of date string
    return date.toISOString().substring(14, 19);
}

const initWebSocket = () => {
    console.log('Trying to open a WebSocket connection...');
    webSocket = new WebSocket(wsUri);

    const handleClose = () => {
        webSocket = null;
        document.getElementById("game").className = "game--loading"
        setTimeout(initWebSocket, 1000);
    }
    const setPongTimeout = () => {
        clearTimeout(wsPongTimeout);
        wsPongTimeout = setTimeout(() => {
            console.log("Did not receive WS message for 8 seconds, closing connection...")
            webSocket.close();
            handleClose();
        }, 4000)
    }
    webSocket.onopen = function () {
        if (this !== webSocket) return;
        console.log("Connection opened");
        setPongTimeout();
    };
    webSocket.onclose = function onClose() {
        if (this !== webSocket) return;
        console.log('Connection closed');
        handleClose();
    }
    webSocket.onmessage = function (evt) {
        if (this !== webSocket) return;
        const split = evt.data.split(" ");
        const id = split[0];
        const value = split[1];

        const intValue = parseInt(value);

        switch (id) {
            case "gameState":
                document.getElementById("game").className = gameStateClassMap[value]


                const newGameState = intValue;
                if (newGameState !== currentGameState) {
                    if (currentGameState == GAME_PLAYING) {
                        console.log("game has ended");
                        addScore(prompt("Name?"), lastScore);
                        updateScoreBoard();
                    }
                }

                currentGameState = newGameState;

                break;
            case "score":
                document.getElementById("score").innerHTML = value
                lastScore = intValue;
            case "time":
                document.getElementById("clock").innerHTML = formatSeconds(parseInt(value))
        }

        setPongTimeout();
    };
}

window.addEventListener('load', initWebSocket);
updateScoreBoard();