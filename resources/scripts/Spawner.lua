-- Spawner: on creation it spawns a prefab and a plain entity into the scene.
local M = {}

function M.OnCreate(entity)
    local spawned = Scene.CreateEntity("SpawnedByLua")
    if spawned then
        spawned:SetPosition(5, 5, 5)
    end

    M.spawned = spawned
end

function M.OnUpdate(entity, dt)
    -- no-op
end

return M
