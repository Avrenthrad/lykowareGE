-- The falling-crates commands, issued by a script instead of read from
-- falling-crates.commands.json (ADR 0042). Both give the same scene, bit for bit:
--   lyko run --scene falling-crates.scene.json --script falling-crates.lua --ticks 120
--   lyko run --scene falling-crates.scene.json --commands falling-crates.commands.json --ticks 120

function on_tick(tick)
  if tick == 10 then
    set_velocity("red", {1, 0, 0})
  elseif tick == 30 then
    spawn{id = "gold", position = {0, 7, 1}, colour = {0.9, 0.65, 0.1}}
  elseif tick == 90 then
    destroy("blue")
  end
end
