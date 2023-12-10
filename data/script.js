const wsUri = `ws://${window.location.hostname}/ws`;
// const wsUri = `ws://192.168.69.152/ws`;

const GAME_LOADING = 0;
const GAME_IDLE = 1;
const GAME_STARTING = 2;
const GAME_PLAYING = 3;

const GAME_END = -1;

const K_SCORES = "scores"

let webSocket
let wsPongTimeout;
let endGameTimeout;
let currentGameState = 0;
let lastScore = 0;

let serialBuffer = ""

let useSerial = false;

const gameStateClassMap = {
    [GAME_LOADING]: "game--loading",
    [GAME_IDLE]: "game--idle",
    [GAME_STARTING]: "game--starting",
    [GAME_PLAYING]: "game--playing",
    [GAME_END]: "game--end"
}

// ChatGPT generated
const mockingMessages = [
    "Pathetic.",
    "Try harder, maybe?",
    "Is this a joke?",
    "Nice attempt, not.",
    "Hug needed. And practice.",
    "Even playing?",
    "Weak sauce.",
    "Game or nap?",
    "Strategy or surrender?",
    "Embarrassing.",
    "Toddler level.",
    "Even trying?",
    "Forgot to score?",
    "A-game? Oh, my.",
    "Game or glitch?",
    "Losing art.",
    "Toddlers score higher.",
    "Aim: last place?",
    "Score says 'ouch.'",
    "Playing or daydreaming?",
    "Score needs therapy.",
    "Warm-up, I hope.",
    "Strategy or surrender?",
    "Attempt, or not.",
    "Enjoy losing?",
    "Aim: last place?",
    "Score says 'meh.'",
    "Game or nap?",
    "Practice at all?",
    "Score wants a refund.",
    "Nap simulator?",
    "Plants grow faster.",
    "On life support.",
    "Even awake?",
    "On a diet - too low.",
    "Not even trying, right?",
    "Apology needed.",
    "Joke or tragedy?",
    "Scoreless wonder.",
    "Caffeine needed.",
    "Pity party?",
    "Exciting paint drying.",
    "Letdown.",
    "Score or sigh?",
    "Reality check.",
    "Practice at all?",
    "Disaster.",
    "Game, not a nap!",
    "Lost. Help it.",
    "Confused up and down?",
    "Scoreless wonder.",
    "Caffeine needed.",
    "Pity party?",
    "Paint drying.",
    "Letdown.",
    "Score or sigh?",
    "Reality check.",
    "Cute attempt.",
    "Forgot how to win?",
    "Disappointment.",
    "Not a lullaby.",
    "Losing streak.",
    "Game or catnap?",
    "Scoreless wonder.",
    "Cry for help.",
    "Aim: bottom?",
    "Pity party?",
    "Timeout needed.",
    "Wake up!",
    "Vacation.",
    "Sad story?",
    "Losing legend.",
    "Tragedy.",
    "Forgot how to play?",
    "Facepalm.",
    "Game or nap?",
    "Letdown.",
    "Even trying?",
    "Redo needed."
];

const getScores = (extraScore) => {
    const rawScores = localStorage.getItem(K_SCORES);
    let arr = []
    try {
        const parsed = JSON.parse(rawScores);
        if (Array.isArray(parsed)) arr = parsed;
    } catch (e) {
        // Empty
    }
    if (extraScore) arr.push(extraScore)
    return arr.sort((a, b) => {
        return (
            a.score > b.score ||
            (
                a.score === b.score &&
                a.timestamp < b.timestamp
            )
        ) ? -1 : 1;
    });
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

const updateScoreBoard = (scores) => {
    const tableBody = document.getElementById("scoreboard-table-body");

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
        if (score.temp) trNode.className = "highlight";
        tableBody.appendChild(trNode);
    }
}

const clearEndGameTimeout = () => {
    clearTimeout(endGameTimeout);
}

const setGameEndTimeout = () => {
    clearEndGameTimeout();
    endGameTimeout = setTimeout(
        () => {
            if (currentGameState === GAME_END) {
                currentGameState = GAME_IDLE;
                updateScoreBoard(getScores());
                syncGameState();
            }
        },
        30000
    )
}

const formatSeconds = (seconds) => {
    const date = new Date(0);
    date.setSeconds(seconds);
    // get mm:ss part of date string
    return date.toISOString().substring(14, 19);
}

const syncGameState = () => {
    document.getElementById("game").className = gameStateClassMap[currentGameState];
}

const handleMessage = (message) => {
    console.log(message)
    const split = message.split(" ");
    const id = split[0];
    const value = split[1];

    const intValue = parseInt(value);

    switch (id) {
        case "gameState":
            const newGameState = intValue;
            if (newGameState !== currentGameState) {

                let finalGameState = newGameState;

                clearEndGameTimeout();

                switch (currentGameState) {
                    case GAME_END:
                        // If new game state is idle, don't change
                        //  visualisation yet
                        if (newGameState === GAME_IDLE) {
                            finalGameState = GAME_END;
                        }
                        break;
                    case GAME_PLAYING:
                        // Game state changed from playing to another,
                        //  assume game has ended
                        finalGameState = GAME_END;
                        setGameEndTimeout();

                        const scores = getScores({
                            score: lastScore,
                            name: '---',
                            timestamp: Date.now(),
                            temp: true
                        });
                        const idx = scores.findIndex(el => el.temp);

                        const newHighScore = idx === 0;
                        const message = newHighScore
                            ? "NEW HIGH SCORE!"
                            : mockingMessages[
                            mockingMessages.length * Math.random() | 0
                            ]
                        document.getElementById("end-text").innerText = message;
                        document.getElementById("end-score").innerText = lastScore;
                        const endPlace = document.getElementById("end-place");
                        endPlace.innerText = `${idx + 1}${nth(idx + 1)}`;
                        const input = document.getElementById("name-input");
                        input.value = ''
                        setTimeout(() => {
                            // Select and focus input element when UI is updated
                            input.select();
                            input.focus();
                        }, 200)
                        updateScoreBoard(scores);
                        document.getElementById("end-title");
                }

                currentGameState = finalGameState;
                syncGameState();
            }

            break;
        case "score":
            document.getElementById("score").innerHTML = value
            lastScore = intValue;
            break;
        case "time":
            document.getElementById("clock").innerHTML = formatSeconds(intValue)
            break;
    }
}

const initWebSocket = () => {
    console.log('Trying to open a WebSocket connection...');
    try {
        webSocket = new WebSocket(wsUri);
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
            handleMessage(evt.data);

            setPongTimeout();
        };
    } catch (e) {
        console.log("Error opening websocket connection: ", e)
    }

    const handleClose = () => {
        webSocket = null;
        if (!useSerial) {
            currentGameState = GAME_LOADING;
            syncGameState();
            setTimeout(initWebSocket, 1000);
        }
    }
    const setPongTimeout = () => {
        clearTimeout(wsPongTimeout);
        wsPongTimeout = setTimeout(() => {
            console.log("Did not receive WS message for 4 seconds, closing connection...")
            webSocket.close();
            handleClose();
        }, 4000)
    }
}

const setInputFieldListeners = () => {
    const input = document.getElementById("name-input");
    input.addEventListener("input", (event) => {
        setGameEndTimeout();
        event.target.value = event.target.value.replace(/\W/g, '');
    });
    input.addEventListener("change", (event) => {
        // Enter pressed
        if (event.target.value) {
            addScore(event.target.value, lastScore);
            currentGameState = GAME_IDLE;
            updateScoreBoard(getScores());
            syncGameState();
        }
    });
}

const openSerialPort = async () => {

    const port = (await navigator.serial.getPorts())[0]

    if (!port) return;

    await port.open({ baudRate: 115200 });

    console.log("Serial USB connection established");
    useSerial = true;

    // Port opened
    const textEncoder = new TextEncoderStream();
    textEncoder.readable.pipeTo(port.writable);
    const writer = textEncoder.writable.getWriter();
    // Just need to send anything to serial port to receive
    //  initial game state
    await writer.write("i");
    writer.releaseLock();

    while (port.readable) {
        try {
            const decoder = new TextDecoderStream();
            port.readable.pipeTo(decoder.writable);
            const inputStream = decoder.readable;
            const reader = inputStream.getReader();


            while (true) {
                const { value, done } = await reader.read();
                if (value) {
                    serialBuffer += value;

                    const split = serialBuffer.split("\n");
                    if (split.length > 1) {
                        const split = serialBuffer.split("\n");
                        serialBuffer = split[split.length - 1];
                        split.slice(0, split.length - 1).forEach(handleMessage);
                    }
                }
                if (done) {
                    reader.releaseLock();
                    break;
                }
            }

        } catch (error) {
            console.warn(error);
        }
    }
}

const setupSerialUsb = () => {

    document.getElementById("usb-btn")
        .addEventListener("click",
            async () => {
                await navigator.serial.requestPort();
                await openSerialPort();
            }
        )

    navigator.serial.addEventListener("connect", () => {
        openSerialPort();
    });

}

initWebSocket();
setInputFieldListeners();
updateScoreBoard(getScores());
if (navigator.serial) {
    setupSerialUsb();
    openSerialPort();
}