-- Verifies that the entity mutation bindings report failure instead of
-- discarding it. A setter whose component or field name is wrong used to do
-- nothing at all and look exactly like it had worked.
--
-- Each result is stored on the script so the C++ side can read it back with
-- ScriptEngine::GetScriptNumber, rather than the test having to trust that the
-- script ran.

local M = {}

-- Results of the successful calls, to confirm the boolean is not simply
-- always-true.
M.SetFloatOK = nil
M.SetBoolOK = nil
M.SetStringOK = nil
M.SetVector3OK = nil
M.RemoveOK = nil

-- Results of the failing calls.
M.SetFloatBad = nil
M.SetBoolBad = nil
M.SetStringBad = nil
M.SetVector3Bad = nil
M.RemoveMissing = nil
M.RemoveUnknown = nil

-- A script written before the bindings reported failure would have called these
-- and discarded the answer; it must still run unchanged.
M.LegacyStillRuns = false

-- Read back at the point the good set happened. The legacy calls below change
-- Friction again, so the test cannot infer the intermediate value afterwards.
M.FrictionAfterGoodSet = nil
M.TranslationAfterGoodSet = nil

function M.OnCreate(entity)
    -- The entity arrives with a Rigidbody and a BoxCollider, so these are real
    -- components with real fields.
    M.SetFloatOK = entity:SetFloat("BoxCollider", "Friction", 0.5)
    M.FrictionAfterGoodSet = entity:GetFloat("BoxCollider", "Friction")
    M.SetBoolOK = entity:SetBool("Rigidbody", "FixedRotation", true)
    M.SetVector3OK = entity:SetVector3("Transform", "Translation", 1, 2, 3)
    local tx, _, tz = entity:GetPosition()
    M.TranslationAfterGoodSet = tx + tz

    entity:AddComponent("Audio")
    M.SetStringOK = entity:SetString("Audio", "SourcePath", "hit.ogg")
    M.RemoveOK = entity:RemoveComponent("Audio")

    -- Mistyped field: "Frikshun" is not "Friction".
    M.SetFloatBad = entity:SetFloat("BoxCollider", "Frikshun", 5)

    -- Mistyped component: there is no such thing.
    M.SetBoolBad = entity:SetBool("Widgit", "Enabled", true)

    -- Right component, wrong field.
    M.SetStringBad = entity:SetString("Audio", "Colour", "red")

    -- Wrong field type entirely.
    M.SetVector3Bad = entity:SetVector3("Transform", "Translaiton", 9, 9, 9)

    -- A component the entity does not carry, and one that does not exist.
    M.RemoveMissing = entity:RemoveComponent("Light")
    M.RemoveUnknown = entity:RemoveComponent("Nonsense")

    -- The old style: calling the setters for effect and ignoring the answer.
    -- Nothing here should raise or otherwise disturb the script.
    entity:SetFloat("BoxCollider", "Friction", 0.25)
    entity:SetPosition(4, 5, 6)
    M.LegacyStillRuns = true
end

return M