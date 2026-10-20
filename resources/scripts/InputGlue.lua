-- Verifies the input bindings (Hazel's ScriptGlue binds Input::IsKeyPressed the
-- same way). The unit-test environment has no window, and the engine's contract
-- is that input queries report false / zero with no window rather than
-- dereferencing a null handle - so the script must see exactly that, as plain
-- values it can hand back to the test.

local M = {}

function M.OnCreate(entity)
    M.KeyDownW = GetKeyDown(87)          -- Key::W
    M.KeyReleasedW = GetKeyReleased(87)
    M.KeyDownSpace = GetKeyDown(32)      -- Key::Space

    M.MouseDownLeft = GetMouseButtonDown(0)   -- Mouse::ButtonLeft
    M.MouseDownRight = GetMouseButtonDown(1)  -- Mouse::ButtonRight
    M.MouseUpLeft = GetMouseButtonUp(0)

    -- The input action map: a script asks for an action by name, and sets
    -- up its own actions when it wants them.
    AddAction("probe_jump")
    M.BoundKey = BindKey("probe_jump", "Space")
    M.ActionPressed = IsActionPressed("probe_jump")
    M.ActionJustPressed = IsActionJustPressed("probe_jump")
    M.ActionAxis = GetActionAxis("probe_left", "probe_right")

    local x, y = GetMousePosition()
    M.MouseX = x
    M.MouseY = y
    M.MouseIsNumber = (type(x) == "number") and (type(y) == "number")
end

return M
