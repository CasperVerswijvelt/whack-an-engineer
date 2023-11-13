const gateway = `ws://${window.location.hostname}/ws`;

const initWebSocket = () => {
    console.log('Trying to open a WebSocket connection...');
    const websocket = new WebSocket(gateway);
    websocket.onopen = () => console.log("Conneciton opened");
    websocket.onclose = () => {
        console.log('Connection closed');
        setTimeout(initWebSocket, 2000);
    };
    websocket.onmessage = (evt) => {
        console.log("Received message", evt.data);
        const split = evt.data.split(" ");
        const id = split[0];
        const value = split[1];

        document.getElementById(id).textContent = value;
    };
}

window.addEventListener('load', initWebSocket);