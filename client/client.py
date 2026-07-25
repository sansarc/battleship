import asyncio
from textwrap import dedent
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical, Grid
from textual.widgets import Button, Input, Label, RichLog
from textual.screen import ModalScreen
from textual import work
from textual.message import Message
from game_screen import GameScreen

from textual.screen import Screen

class IntroScreen(Screen):
    CSS = """
    IntroScreen {
        align: center middle;
        background: $surface;
    }
    #ascii-art {
        text-style: bold;
        color: $text;
        margin-bottom: 2;
    }
    #btn-enter {
        width: 30;
        margin-left: 23;
    }
    """

    def compose(self) -> ComposeResult:
        # raw string altrimenti \ viene interpretato come escape
        art = dedent(r"""
        
                ______       _   _   _           _     _        
                | ___ \     | | | | | |         | |   (_)       
                | |_/ / __ _| |_| |_| | ___  ___| |__  _ _ __   
                | ___ \/ _` | __| __| |/ _ \/ __| '_ \| | '_ \  
                | |_/ / (_| | |_| |_| |  __/\__ \ | | | | |_) | 
                \____/ \__,_|\__|\__|_|\___||___/_| |_|_| .__/  
                                                        | |     
                                                        |_|     
                                                       
                                           |
                                     # #  ( )
                                  ___#_#___|__
                              _  |____________|  _
                       _=====| | |            | | |==== _
                 =====| |.---------------------------. | |====
   <--------------------'   .  .  .  .  .  .  .  .   '--------------/
     \                                                             /
      \___________________________________________________________/
  wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww
   wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww 


        """).strip("\n")

        yield Label(art, id="ascii-art")
        yield Button("ENTER LOBBY", id="btn-enter", variant="success")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "btn-enter":
            self.app.pop_screen()

# custom message di textual per passare i dati letti dal socket (async) alla UI
class ServerMessage(Message):
    def __init__(self, payload: str) -> None:
        self.payload = payload.strip()
        super().__init__()

class JoinRequestModal(ModalScreen[bool]):
    CSS = """
    JoinRequestModal {
        align: center middle;
    }
    #dialog {
        grid-size: 2;
        grid-gutter: 1 2;
        grid-rows: 1fr 3;
        padding: 0 1;
        width: 60;
        height: 11;
        border: thick $primary 80%;
        background: $surface;
    }
    #question {
        column-span: 2;
        height: 1fr;
        width: 1fr;
        content-align: center middle;
        text-style: bold;
    }
    Button {
        width: 100%;
    }
    """

    def __init__(self, opponent_id: str, match_id: str):
        super().__init__()
        self.opponent_id = opponent_id
        self.match_id = match_id

    def compose(self) -> ComposeResult:
        yield Grid(
            Label(f"Client {self.opponent_id} wants to join Match {self.match_id}. Accept?", id="question"),
            Button("Accept", variant="success", id="accept"),
            Button("Reject", variant="error", id="reject"),
            id="dialog"
        )

    def on_button_pressed(self, event: Button.Pressed) -> None:
        # dismiss() chiude il modal e ritorna il valore booleano alla funzione di callback che lo ha invocato
        if event.button.id == "accept":
            self.dismiss(True)
        else:
            self.dismiss(False)


class BattleshipClient(App):
    CSS = """
    #top-bar {
        height: 3;
        dock: top;
        background: $boost;
        layout: horizontal;
    }
    #top-bar Input { width: 20; }
    #top-bar Button { margin: 0 1; }
    #top-bar Label { margin: 1 2; text-style: bold; color: $success; }
    
    #main-container { layout: horizontal; height: 1fr; }
    #actions-pane { width: 30; padding: 1; border: solid $primary; }
    #actions-pane Button, #actions-pane Input { width: 100%; margin-bottom: 1; }
    
    #logs-pane { width: 1fr; border: solid $secondary; }
    RichLog { height: 1fr; }
    #cmd-input { dock: bottom; margin-top: 1; }
    """

    def __init__(self):
        super().__init__()
        self.reader = None
        self.writer = None
        self.connected = False

    def compose(self) -> ComposeResult:
        # top banner con gestione della connessione
        with Horizontal(id="top-bar"):
            yield Label("BATTLESHIP LOBBY", id="title-label")
            yield Input(value="127.0.0.1", id="host-input", placeholder="Host")
            yield Input(value="8080", id="port-input", placeholder="Port")
            yield Button("Connect", id="btn-connect", variant="success")
            yield Button("Disconnect", id="btn-disconnect", variant="error")
            yield Label("Client ID: N/A", id="client-id-label")

        # main lobby con comandi di lato
        with Horizontal(id="main-container"):
            with Vertical(id="actions-pane"):
                yield Button("Create Match", id="btn-create", variant="primary")
                yield Button("Update Matches", id="btn-list", variant="primary")

                yield Input(placeholder="Match ID to Join", id="join-id-input")
                yield Button("Join Match", id="btn-join", variant="warning")

            with Vertical(id="logs-pane"):
                yield RichLog(id="server-log", highlight=True, markup=True)
                yield Input(placeholder="Raw Command (e.g. ACCEPT 0, JOIN 1, REJECT 0)", id="cmd-input")

    def on_mount(self) -> None:
        self.push_screen(IntroScreen())
        self.log_msg("[system] Client started. Waiting to connect...")

    def log_msg(self, msg: str):
        # se vi è una partita in corso i log vengono girati a GameScreen, altrimenti sono scritti nella main lobby
        if isinstance(self.screen, GameScreen):
            self.screen.log_msg(msg)
            return

        try: self.query_one("#server-log", RichLog).write(msg)
        except Exception: pass

    async def on_button_pressed(self, event: Button.Pressed) -> None:
        btn_id = event.button.id

        if btn_id == "btn-connect":
            if not self.connected:
                host = self.query_one("#host-input", Input).value
                port = int(self.query_one("#port-input", Input).value)
                self.connect_to_server(host, port)

        elif btn_id == "btn-disconnect":
            if self.connected and self.writer:
                self.writer.close()
                await self.writer.wait_closed()

        elif btn_id == "btn-create":
            self.send_command("CREATE")

        elif btn_id == "btn-list":
            self.log_msg("\n--- MATCH LIST ---")
            self.send_command("LIST")

        elif btn_id == "btn-join":
            join_input = self.query_one("#join-id-input", Input)
            match_id = join_input.value.strip()
            if match_id.isdigit():
                self.send_command(f"JOIN {match_id}")
                join_input.value = ""
            else:
                self.log_msg("[error] Match ID must be a valid integer.")

    async def on_input_submitted(self, event: Input.Submitted) -> None:
        if event.input.id == "cmd-input":
            cmd = event.value.strip()
            if cmd:
                self.send_command(cmd)
                event.input.value = ""
        elif event.input.id == "join-id-input":
            match_id = event.value.strip()
            if match_id.isdigit():
                self.send_command(f"JOIN {match_id}")
                event.input.value = ""
            else:
                self.log_msg("[error] Match ID must be a valid integer.")

    def send_command(self, cmd: str) -> None:
        if not self.connected or not self.writer:
            self.log_msg("[error] Not connected to server.")
            return

        self.log_msg(f"[client ->] {cmd}")
        self.writer.write((cmd + "\n").encode())

    # @work esegue la routine in background rispetto all'event loop di Textual, evitando freeze della UI
    @work(exclusive=True, thread=False)
    async def connect_to_server(self, host: str, port: int) -> None:
        self.log_msg(f"[system] Connecting to {host}:{port}...")
        try:
            self.reader, self.writer = await asyncio.open_connection(host, port)
            self.connected = True
            self.log_msg("[system] Connected successfully.")
            self.query_one("#client-id-label", Label).update("Client ID: Unknown")

            while True:
                line = await self.reader.readline()
                if not line:
                    break
                self.post_message(ServerMessage(line.decode(errors="ignore")))

        except Exception as e:
            self.log_msg(f"[error] Connection failed: {e}")
        finally:
            self.disconnected()

    def on_server_message(self, event: ServerMessage) -> None:
        payload = event.payload

        # delega il parsing dei messaggi del server al controller della partita se la fase di lobby è finita
        if isinstance(self.screen, GameScreen):
            self.screen.process_server_message(payload)
            return

        self.log_msg(f"[<- server] {payload}")

        # parsing
        if payload.startswith("HELLO"):
            parts = payload.split()
            if len(parts) >= 2:
                client_id = parts[1]
                self.query_one("#client-id-label", Label).update(f"Client ID: {client_id}")

        elif payload.startswith("JOIN MATCH REQUEST"):
            # JOIN MATCH REQUEST FROM <fd> (match_id=<id>)
            try:
                parts = payload.split()
                opponent_id = parts[4]
                match_id = payload.split("match_id=")[1].strip(")")

                # callback nel modal, viene chiamata da self.dismiss(bool) dentro JoinRequestModal
                def check_response(accepted: bool) -> None:
                    cmd = f"ACCEPT {match_id}" if accepted else f"REJECT {match_id}"
                    self.send_command(cmd)

                self.push_screen(JoinRequestModal(opponent_id, match_id), check_response)

            except IndexError:
                self.log_msg("[error] Failed to parse incoming join request. Format unexpected.")

        elif payload.startswith("OK GAME_START"):
            try:
                match_id = payload.split("match_id=")[1].strip(")")
                self.log_msg(f"[system] Match {match_id} starting. Transitioning to game board...")
                self.push_screen(GameScreen(match_id))
            except IndexError:
                self.log_msg("[error] Failed to parse game start message.")

    def disconnected(self) -> None:
        if not self.connected: return

        self.connected = False
        self.reader = None
        self.writer = None

        try:
            self.query_one("#client-id-label", Label).update("Client ID: N/A")
            self.log_msg("[system] Disconnected from server.")
        except Exception:
            pass

        self.log_msg("[system] Disconnected from server.")

if __name__ == "__main__":
    app = BattleshipClient()
    app.run()