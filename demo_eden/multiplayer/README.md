# Eden multiplayer (SpacetimeDB)

Players on the same planet see each other move and animate, and every dig or placed block is shared and kept.

- `server/`: the SpacetimeDB module (C#). Tables `player` (position, facing, animation state, online) and
  `voxel_edit` (every dig/place, replayed to anyone who joins). Reducers `set_name`, `update_player` and
  `add_voxel_edit` check their input: names 1-24 characters, finite positions, edits within 12 m of the player,
  radius at most 2 m.
- `eden_net.gd` (EdenNet): the game's client. It uses SpacetimeDB's JSON WebSocket protocol
  (`v1.json.spacetimedb`) directly from GDScript, so no SDK or C# build is needed on the Godot side.
- `eden_remote_player.gd` (EdenRemotePlayer): another player's avatar. It uses the same model and procedural
  animator and moves smoothly between updates.

## Run it

```
spacetime start --listen-addr 127.0.0.1:3180 --data-dir demo_eden/multiplayer/.stdb
spacetime publish eden --module-path demo_eden/multiplayer/server/spacetimedb -s http://127.0.0.1:3180
```

Then play `eden_play.tscn` with the EdenPlayer's **Scene Setup > Online** ticked, or run the game with
`-- --online [--name=You] [--server=ws://host:3180]`. Start a second copy the same way to see two players.
The HUD shows the connection state and how many players are online.

Port 3180 is used because 3000, SpacetimeDB's default, is often taken by other dev servers. To serve other
machines, listen on `0.0.0.0:3180` and point clients at `--server=ws://<this machine>:3180`.

To wipe the world's edits and players, publish again with `--delete-data`.

## Test

```
python demo_eden/multiplayer/mp_test.py bin/godot.windows.editor.x86_64.console.exe
```

The test starts SpacetimeDB if needed, publishes the module fresh, runs the game online (`_mp_test.gd`), and adds
a bot player (`mp_bot.py`) that walks circles around you and digs a hole next to you. It passes when:

- the game sees the bot's avatar and the bot's dig;
- the bot sees the game's player move;
- both reducers were accepted.
