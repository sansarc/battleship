from __future__ import annotations

from typing import TYPE_CHECKING, Optional, Tuple, cast

from textual.app import ComposeResult
from textual.containers import Grid, Horizontal, Vertical
from textual.screen import Screen
from textual.widgets import Button, Label, RichLog

if TYPE_CHECKING:
    from client import BattleshipClient  # avoids circular import at runtime

GRID_SIZE = 10
SHIPS_PER_PLAYER = 3
# le lunghezze delle navi sono decise dal server, questa lista serve al client per i prompt e per sapere quante celle colorare
SHIPS = [("Battleship", 4), ("Cruiser", 3), ("Destroyer", 2)]


class GridCell(Button):
    # la cella della griglia estende Button per sfruttare l'event di default
    def __init__(self, x: int, y: int, prefix: str) -> None:
        super().__init__("\u00b7", id=f"{prefix}-{x}-{y}")
        self.x = x
        self.y = y
        self.can_focus = False


class GameScreen(Screen):
    CSS_PATH = "game_screen.tcss"

    def __init__(self, match_id: str) -> None:
        super().__init__()
        self.match_id = match_id
        # FSM solo per controllare i permessi UI
        self.phase = "placement"  # placement | waiting | combat | finished
        self.orientation = "H"
        self.ship_index = 0
        # tupla tempo
        self.pending_ship: Optional[Tuple[int, int, int, str]] = None
        self.my_turn = False
        # matrici per non chiamare continuamente lo stato della UI
        self.own_grid = [["."] * GRID_SIZE for _ in range(GRID_SIZE)]
        self.target_grid = [["."] * GRID_SIZE for _ in range(GRID_SIZE)]

    # ---------- compose ----------

    def compose(self) -> ComposeResult:
        yield Label(f"MATCH {self.match_id} - PLACEMENT PHASE", id="match-header")

        with Horizontal(id="boards"):
            with Vertical(classes="board-pane"):
                yield Label("YOUR BOARD", classes="board-title")
                with Grid(classes="board-grid", id="own-grid"):
                    for x in range(GRID_SIZE):
                        for y in range(GRID_SIZE):
                            yield GridCell(x, y, "own")

            with Vertical(classes="board-pane"):
                yield Label("OPPONENT BOARD", classes="board-title")
                with Grid(classes="board-grid", id="target-grid"):
                    for x in range(GRID_SIZE):
                        for y in range(GRID_SIZE):
                            yield GridCell(x, y, "target")

            yield RichLog(id="game-log", highlight=True, markup=True)

        with Horizontal(id="side-panel"):
            with Horizontal(id="button-row"):
                yield Button(f"Toggle Orientation ({self.orientation})", id="btn-orientation", variant="primary")
                yield Button("Leave Match", id="btn-leave", variant="error")
                yield Button("Request Rematch", id="btn-rematch", variant="success")
                yield Button("Back to Lobby", id="btn-back", variant="warning")

            yield Label("Legend: \u25a0 ship   X hit   o miss", id="legend-label")
            yield Label(self._placement_prompt(), id="status-label")

    def on_mount(self) -> None:
        self.log_msg(f"[system] Entered match {self.match_id}. Place your ships.")
        self._set_rematch_visible(False)
        self._set_back_visible(False)
        self._set_target_enabled(False)

    # ---------- helpers ----------

    def _client(self) -> "BattleshipClient":
        return cast("BattleshipClient", self.app)

    def log_msg(self, msg: str) -> None:
        self.query_one("#game-log", RichLog).write(msg)

    def _placement_prompt(self) -> str:
        if self.ship_index >= len(SHIPS):
            return "All ships placed. Waiting..."
        name, length = SHIPS[self.ship_index]
        return f"Place your {name} ({length} cells)\nOrientation: {self.orientation}\nClick a starting cell on YOUR board."

    def _set_status(self, text: str) -> None:
        self.query_one("#status-label", Label).update(text)

    def _set_header(self, text: str) -> None:
        self.query_one("#match-header", Label).update(text)

    def _set_rematch_visible(self, visible: bool) -> None:
        self.query_one("#btn-rematch", Button).display = visible

    def _set_back_visible(self, visible: bool) -> None:
        self.query_one("#btn-back", Button).display = visible

    def _set_orientation_enabled(self, enabled: bool) -> None:
        self.query_one("#btn-orientation", Button).display = enabled

    def _cell(self, prefix: str, x: int, y: int) -> GridCell:
        return self.query_one(f"#{prefix}-{x}-{y}", GridCell)

    def _paint_cell(self, prefix: str, x: int, y: int, state: str) -> None:
        # pulisce le vecchie classi CSS e imposta quelle corrette disabilitando il bottone se colpito/mancato
        cell = self._cell(prefix, x, y)
        cell.remove_class("cell-ship", "cell-hit", "cell-miss")
        cell.disabled = False

        labels = {"ship": ("\u25a0", "cell-ship", False), "hit": ("X", "cell-hit", True), "miss": ("o", "cell-miss", True)}
        if state in labels:
            label, css_class, disabled = labels[state]
            cell.label = label
            cell.add_class(css_class)
            cell.disabled = disabled
        else:
            cell.label = "\u00b7"

        cell.refresh(layout=True)

    def _set_target_enabled(self, enabled: bool) -> None:
        for x in range(GRID_SIZE):
            for y in range(GRID_SIZE):
                if self.target_grid[x][y] == ".":
                    self._cell("target", x, y).disabled = not enabled

    def _set_own_enabled(self, enabled: bool) -> None:
        for x in range(GRID_SIZE):
            for y in range(GRID_SIZE):
                self._cell("own", x, y).disabled = not enabled

    # ---------- input ----------

    def on_button_pressed(self, event: Button.Pressed) -> None:
        btn_id = event.button.id

        if btn_id == "btn-orientation":
            self.orientation = "V" if self.orientation == "H" else "H"
            event.button.label = f"Toggle Orientation ({self.orientation})"
            if self.phase == "placement":
                self._set_status(self._placement_prompt())
            return

        if btn_id == "btn-leave":
            self._client().send_command(f"LEAVE {self.match_id}")
            return

        if btn_id == "btn-rematch":
            self._client().send_command(f"REMATCH {self.match_id}")
            self._set_status("Rematch requested. Waiting for opponent...")
            return

        if btn_id == "btn-back":
            self.app.pop_screen()
            return

        if isinstance(event.button, GridCell):
            self._handle_cell_click(event.button)

    def _handle_cell_click(self, cell: GridCell) -> None:
        prefix = cell.id.split("-")[0]

        if prefix == "own" and self.phase == "placement":
            if self.ship_index >= len(SHIPS):
                return
            _, length = SHIPS[self.ship_index]

            # salva temporaneamente i dati, la colorazione avviene solo quando riceve "OK SHIP PLACED"
            self.pending_ship = (cell.x, cell.y, length, self.orientation)
            self._client().send_command(f"PLACE {self.match_id} {cell.x} {cell.y} {self.orientation}")

        elif prefix == "target" and self.phase == "combat":
            # blocca l'input se non è il turno del giocatore o se spara su una cella già colpita
            if not self.my_turn:
                self.log_msg("[error] It's not your turn.")
                return
            if self.target_grid[cell.x][cell.y] != ".":
                return
            self._client().send_command(f"SHOOT {self.match_id} {cell.x} {cell.y}")

    # ---------- server message routing ----------

    def process_server_message(self, payload: str) -> None:
        self.log_msg(f"[<- server] {payload}")

        # variabile per instradare i messaggi TCP del server alle funzioni della classe
        handlers = {
            "OK WAITING_OPPONENT": self._on_waiting_opponent,
            "OK COMBAT_START YOUR_TURN": lambda: self._start_combat(True),
            "OK COMBAT_START OPPONENT_TURN": lambda: self._start_combat(False),
            "OK REMATCH_PENDING": lambda: self._set_status("Rematch requested. Waiting for opponent..."),
            "OK GAME_START_PLACEMENT": self._reset_for_rematch,
            "OK LEFT_ACK": self._on_left_ack,
            "TURN_END": self._on_turn_end,
            "YOUR_TURN": self._on_your_turn,
        }

        if payload in handlers:
            handlers[payload]()
        elif payload.startswith("OK SHIP PLACED"):
            self._on_ship_placed(payload)
        elif payload.startswith("OK HIT"):
            self._on_shot_result(payload, True)
        elif payload.startswith("OK MISS"):
            self._on_shot_result(payload, False)
        elif payload.startswith("OPPONENT_HIT"):
            self._on_opponent_shot(payload, True)
        elif payload.startswith("OPPONENT_MISS"):
            self._on_opponent_shot(payload, False)
        elif payload.startswith("GAME_OVER"):
            self._on_game_over(payload)
        elif payload.startswith("ERR OPPONENT_LEFT") or payload.startswith("ERR OPPONENT_DISCONNECTED"):
            self._on_opponent_gone(payload)
        elif payload.startswith("ERR"):
            self._set_status(f"Error: {payload}")

    def _on_turn_end(self) -> None:
        self.my_turn = False
        self._set_target_enabled(False)
        self._set_status("Shot resolved. Waiting for opponent's move...")
        self._set_header(f"MATCH {self.match_id} - OPPONENT'S TURN")

    def _on_your_turn(self) -> None:
        self.my_turn = True
        self._set_target_enabled(True)
        self._set_status("Your turn! Click a cell on the opponent board.")
        self._set_header(f"MATCH {self.match_id} - YOUR TURN")

    def _on_left_ack(self) -> None:
        self.log_msg("[system] You left the match.")
        self.app.pop_screen()

    def _on_ship_placed(self, payload: str) -> None:
        if self.pending_ship is None:
            return

        # il server ha confermato il posizionamento, ora si può disegnare l'intera nave sulla griglia
        x, y, length, orientation = self.pending_ship
        for i in range(length):
            if orientation.upper() == "H":
                self.own_grid[x][y + i] = "S"
                self._paint_cell("own", x, y + i, "ship")
            else:
                self.own_grid[x + i][y] = "S"
                self._paint_cell("own", x + i, y, "ship")

        self.pending_ship = None
        self.ship_index += 1
        self._set_status(
            "All ships placed. Waiting for server confirmation..."
            if self.ship_index >= len(SHIPS)
            else self._placement_prompt()
        )

    def _on_waiting_opponent(self) -> None:
        self.phase = "waiting"
        self._set_own_enabled(False)
        self._set_orientation_enabled(False)
        self._set_status("All ships placed. Waiting for opponent to finish placing ships...")
        self._set_header(f"MATCH {self.match_id} - WAITING FOR OPPONENT")

    def _start_combat(self, my_turn: bool) -> None:
        self.phase = "combat"
        self.my_turn = my_turn
        self._set_own_enabled(False)
        self._set_target_enabled(my_turn)
        self._set_status(
            "Combat started! Your turn - fire at the opponent board."
            if my_turn
            else "Combat started! Waiting for opponent's move..."
        )
        turn_label = "YOUR TURN" if my_turn else "OPPONENT'S TURN"
        self._set_header(f"MATCH {self.match_id} - {turn_label}")

    def _on_shot_result(self, payload: str, hit: bool) -> None:
        _, _, x, y = payload.split()
        x, y = int(x), int(y)
        self.target_grid[x][y] = "X" if hit else "O"
        self._paint_cell("target", x, y, "hit" if hit else "miss")
        self._set_status("Direct hit!" if hit else "Miss.")

    def _on_opponent_shot(self, payload: str, hit: bool) -> None:
        _, x, y = payload.split()
        x, y = int(x), int(y)
        self.own_grid[x][y] = "X" if hit else "O"
        self._paint_cell("own", x, y, "hit" if hit else "miss")

    def _on_game_over(self, payload: str) -> None:
        self.phase = "finished"
        won = payload.split()[-1] == "WIN"
        self._set_target_enabled(False)
        self._set_own_enabled(False)
        self._set_status("You won! Request a rematch or leave the match." if won else "You lost. Request a rematch or leave the match.")
        self._set_header(f"MATCH {self.match_id} - {'VICTORY' if won else 'DEFEAT'}")
        self._set_rematch_visible(True)

    def _on_opponent_gone(self, payload: str) -> None:
        self.phase = "finished"
        self._set_target_enabled(False)
        self._set_own_enabled(False)
        self._set_status(f"{payload}. The match has ended.")
        self._set_header(f"MATCH {self.match_id} - OPPONENT GONE")
        self._set_rematch_visible(False)
        self._set_back_visible(True)

    def _reset_for_rematch(self) -> None:
        self.phase = "placement"
        self.orientation = "H"
        self.ship_index = 0
        self.pending_ship = None
        self.my_turn = False
        self.own_grid = [["."] * GRID_SIZE for _ in range(GRID_SIZE)]
        self.target_grid = [["."] * GRID_SIZE for _ in range(GRID_SIZE)]

        # sfrutta i worker per ricostruire i componenti della griglia in maniera asincrona
        self.run_worker(self._rebuild_boards(), exclusive=True)

        self._set_orientation_enabled(True)
        self.query_one("#btn-orientation", Button).label = f"Toggle Orientation ({self.orientation})"
        self._set_rematch_visible(False)
        self._set_back_visible(False)
        self._set_header(f"MATCH {self.match_id} - PLACEMENT PHASE")
        self._set_status(self._placement_prompt())
        self.log_msg("[system] Rematch starting! Place your ships again.")

    async def _rebuild_boards(self) -> None:
        own_grid = self.query_one("#own-grid", Grid)
        target_grid = self.query_one("#target-grid", Grid)

        await own_grid.remove_children()
        await target_grid.remove_children()

        await own_grid.mount_all(GridCell(x, y, "own") for x in range(GRID_SIZE) for y in range(GRID_SIZE))
        await target_grid.mount_all(GridCell(x, y, "target") for x in range(GRID_SIZE) for y in range(GRID_SIZE))

        self._set_own_enabled(True)
        self._set_target_enabled(False)