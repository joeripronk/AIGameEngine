-- Tetris Game Script
-- Manages Tetris game state and piece control

local M = {}
M.GameState = "Menu"  -- Menu, Playing, Paused, GameOver
M.PiecePosition = {x = 3, y = 0}
M.NextPieceType = 1
M.Score = 0
M.Level = 1
M.LinesCleared = 0
M.Combo = 0

-- Piece types: 0=I, 1=O, 2=T, 3=S, 4=Z, 5=J, 6=L
local kPieceColors = {
    {0, 1, 1},  -- I: cyan
    {1, 1, 0},  -- O: yellow
    {1, 0, 1},  -- T: purple
    {0, 1, 0},  -- S: green
    {1, 0, 0},  -- Z: red
    {0, 0, 1},  -- J: blue
    {1, 0.5, 0},-- L: orange
}

function M.OnCreate(entity)
    M.GameState = "Menu"
    M.PiecePosition = {x = 3, y = 0}
    M.NextPieceType = math.random(0, 6)
    M.Score = 0
    M.Level = 1
    M.LinesCleared = 0
    M.Combo = 0
    
    -- Set initial piece color
    local color = kPieceColors[M.NextPieceType]
    entity:SetVector3("PBRMaterial", "Albedo", color[1], color[2], color[3])
    
    Log("Tetris game started")
end

function M.OnUpdate(entity, dt)
    if M.GameState == "Playing" then
        M.UpdatePiece(entity, dt)
        M.CheckGameState(entity)
    elseif M.GameState == "Paused" then
        -- Do nothing
    end
end

function M.OnDestroy(entity)
    Log("Tetris game destroyed")
end

function M.UpdatePiece(entity, dt)
    -- Update piece drop timer
    M.PieceDropTimer = M.PieceDropTimer + dt
    if M.PieceDropTimer >= 0.5 then
        M.PieceDropTimer = 0
        -- Move piece down
        M.PiecePosition.y = M.PiecePosition.y + 1
    end
end

function M.CheckGameState(entity)
    -- Check if piece is at bottom
    if M.PiecePosition.y >= 19 then
        M.GameState = "GameOver"
        Log("Game Over! Final score: " .. M.Score)
    end
end

-- Control functions exposed to Lua
function M.StartGame()
    M.GameState = "Playing"
    Log("Game started")
end

function M.PauseGame()
    if M.GameState == "Playing" then
        M.GameState = "Paused"
        Log("Game paused")
    end
end

function M.ResumeGame()
    if M.GameState == "Paused" then
        M.GameState = "Playing"
        Log("Game resumed")
    end
end

function M.TogglePause()
    if M.GameState == "Playing" then
        M.PauseGame()
    elseif M.GameState == "Paused" then
        M.ResumeGame()
    end
end

function M.MovePiece(direction)
    if M.GameState ~= "Playing" then
        return false
    end
    
    if direction == "left" then
        if M.PiecePosition.x > 0 then
            M.PiecePosition.x = M.PiecePosition.x - 1
            return true
        end
    elseif direction == "right" then
        if M.PiecePosition.x < 9 then
            M.PiecePosition.x = M.PiecePosition.x + 1
            return true
        end
    elseif direction == "down" then
        M.PiecePosition.y = M.PiecePosition.y + 1
        M.PieceDropTimer = 0
        return true
    elseif direction == "rotate" then
        -- Rotate piece
        M.PieceRotation = (M.PieceRotation or 0) + 1
        return true
    elseif direction == "hardDrop" then
        -- Hard drop to bottom
        while M.PiecePosition.y < 19 do
            M.PiecePosition.y = M.PiecePosition.y + 1
        end
        M.PieceDropTimer = 0
        return true
    end
    
    return false
end

function M.GetGameState()
    return M.GameState
end

function M.GetScore()
    return M.Score
end

function M.GetLevel()
    return M.Level
end

function M.GetPiecePosition()
    return M.PiecePosition.x, M.PiecePosition.y
end

return M
