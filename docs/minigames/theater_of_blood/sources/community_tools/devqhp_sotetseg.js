/* This code is pretty sloppy right now. Sorry for the mess. */

function showAbout() {
	alert("Feel free to message suggestions to me on discord at qhp#5615.\n\n" +
		"- Special thanks to Ardames, De0, and Hato for advice on the game's mechanics.\n" +
		"- Additional thanks to Deflne_Alive, LucidDream, SNIPERBDS, xZact, and the WeDoRaids discord for sharing mazes from which I could establish rules for maze generation.");
}

function showInstructions() {
	alert(
		"1. You must start on the first tile (your character waits 1 tile south).\n" +
		"2. You must finish on the last tile.\n" +
		"3. You must not damage any of your teammates (mistakes marked red).\n" +
		"4. If you don't move for a tick, the tile you stall on is colored yellow.\n\n" +
		"Movement mechanics work just as they do in OSRS and are processed every tick (600ms).\n" +
		"White circles show where your character existed each tick while traversing the maze.\n" +
		"Blue numbers show your path, while orange numbers show the calculated optimal path.\n" +
		"There may be more than one optimal path, so as long as you're making par you're doing great!"
	);
}

const tick_length  = 600;

const maze_width   = 14;
const maze_height  = 15;
const max_x_change = 5;
const path_turns   = 8;
const tornado_row  = 4;

var viewport_height = window.innerHeight;
var viewport_width = window.innerWidth;
var view_ratio = viewport_width / viewport_height;

var tile_size      = 40;
var tile_stroke  = tile_size/25;
var solv_fontsize  = 15*(tile_size/40);
var offset_optimal = solv_fontsize/2;
var offset_user    = -offset_optimal;

const color_mazeback = "#323232";
const color_tilepath = "#961919";
const color_tilenogo = "#C8C8C8";
const color_tileplay = "#77DD77";
const color_tilenext = "#C8C8C8";
const color_tilesolv = "#6495ED";
const color_tilestal = "#FFFF00";
const color_linesolv = "#FF4500";
const color_lineplay = "#6495ED";
const color_circmove = "#FFFFFF";
const color_circpass = "#008000";
const color_circfail = "#DC143C";
const solv_font      = "Arial";

var canvas = document.getElementById("sotetseg-maze");
var ctx = canvas.getContext("2d");
canvas.width = tile_size * maze_width;
canvas.height = tile_size * (maze_height); // need +1 for the extra row at the top to run off the maze, if desired.

var imgTornado = new Image();
imgTornado.src = "tornado.png";

class Point {
	constructor(x, y) {
		this.x = x;
		this.y = y;
	}
}

function resize() {
	let viewport_height = window.innerHeight;
	let viewport_width = window.innerWidth;

	if (viewport_width / viewport_height < 0.77) {
		if (viewport_width < 620) {
			tile_size = (viewport_width - 20)/maze_width; // 30 is just buffer space
		}
	} else {
		if (viewport_height < 800) {
			tile_size = (viewport_height - 200)/maze_height; // 200 is buffer space for the buttons/text above/below the maze
		}
	}

	tile_stroke  = tile_size/25;
	solv_fontsize  = 15*(tile_size/40);
	offset_optimal = solv_fontsize/2;
	offset_user    = -offset_optimal;

	canvas.width = tile_size * maze_width;
	canvas.height = tile_size * (maze_height); // need +1 for the extra row at the top to run off the maze, if desired.

	drawState();
}

function getTileClicked(event) {
	let rect = canvas.getBoundingClientRect();
	let pixel_x = event.clientX - rect.left;
	let pixel_y = event.clientY - rect.top;
	let tile_x = Math.floor(pixel_x / tile_size);
	let tile_y = Math.floor(pixel_y / tile_size);
	return { x: tile_x, y: tile_y };
}

function randRange(a, b) {
	return Math.floor(Math.random() * (b - a + 1)) + a;
}

function drawPathTile(x, y) {
	let pos_x = tile_size * x;
	let pos_y = tile_size * y;
	if (maze[x][y]) {
		ctx.fillStyle = color_circpass;
	} else {
		ctx.fillStyle = color_circfail;
		team_damaged = true;
	}
	ctx.beginPath(pos_x, pos_y, pos_x+tile_size, pos_y+tile_size);
	ctx.arc(pos_x+tile_size/2, pos_y+tile_size/2, tile_size/4, 0, 2*Math.PI);
	ctx.fill();
}

function drawMoveTile(x, y) {
	let pos_x = tile_size * x;
	let pos_y = tile_size * y;
	ctx.strokeStyle = color_circmove;
	ctx.beginPath(pos_x, pos_y, pos_x+tile_size, pos_y+tile_size);
	ctx.arc(pos_x+tile_size/2, pos_y+tile_size/2, tile_size/3.4, 0, 2*Math.PI);
	ctx.stroke();
}

function drawTargetTile() {
	let pos_x = tile_size * targeted_tile.x;
	let pos_y = tile_size * targeted_tile.y;
	ctx.fillStyle = color_tilesolv;
	ctx.fillRect(pos_x, pos_y, tile_size, tile_size);
	ctx.fillStyle = color_mazeback;
	ctx.fillRect(
		pos_x + tile_stroke,
		pos_y + tile_stroke,
		tile_size - tile_stroke * 2,
		tile_size - tile_stroke * 2
	);
	ctx.beginPath(pos_x, pos_y, pos_x+tile_size, pos_y+tile_size);
	ctx.arc(pos_x+tile_size/2, pos_y+tile_size/2, tile_size/3.4, 0, 2*Math.PI);
	ctx.lineWidth = tile_stroke*1.2;
	ctx.strokeStyle = color_tilesolv;
	ctx.stroke();
}

function drawMazeTile(x, y, color_tile) {
	let pos_x = tile_size * x;
	let pos_y = tile_size * y;
	ctx.fillStyle = color_tile;
	ctx.fillRect(pos_x, pos_y, tile_size, tile_size);
	ctx.fillStyle = color_mazeback;
	ctx.fillRect(
		pos_x + tile_stroke,
		pos_y + tile_stroke,
		tile_size - tile_stroke * 2,
		tile_size - tile_stroke * 2
	);
	ctx.beginPath(pos_x, pos_y, pos_x+tile_size, pos_y+tile_size);
	ctx.arc(pos_x+tile_size/2, pos_y+tile_size/2, tile_size/3.4, 0, 2*Math.PI);
	ctx.lineWidth = tile_stroke*1.2;
	ctx.strokeStyle = color_tile;
	ctx.stroke();
}

function drawMaze() {
	for (let x = 0; x < maze.length; x++) {
		for (let y = 0; y < maze[x].length; y++) {
			drawMazeTile(x, y, maze[x][y] ? color_tilepath : color_tilenogo);
		}
	}
}

function pathWeighting() {
	weighted_maze = Array(maze_width);
	for (let x = 0; x < maze_width; x++) {
		weighted_maze[x] = Array(maze_height);
		for (let y = 0; y < maze_height; y++) {
			weighted_maze[x][y] = 0;
		}
	}
	let t_pos = new Point(-1, -1);
	let p_pos = new Point(-1, -1);
	let c_pos = new Point(seed[0], maze_height - 1)
	counter = 1;
	while (c_pos) {
		path_coordinates.push(new Point(c_pos.x, c_pos.y));
		weighted_maze[c_pos.x][c_pos.y] = counter;
		counter += 1
		t_pos.x = p_pos.x;
		t_pos.y = p_pos.y;
		p_pos.x = c_pos.x;
		p_pos.y = c_pos.y;
		c_pos = getNextPathTile(c_pos, t_pos);
	}
}

function makeSeededMaze(seed) {
	let maze = new Array(maze_width);
	for (let x = 0; x < maze.length; x++) {
		maze[x] = new Array(maze_height);
	}
	for (let x = 0; x < maze.length; x++) {
		for (let y = 0; y < maze[x].length; y++) {
			maze[x][y] = false;
		}
	}
	let next_x = -1;
	let s = 0;
	let x = seed[s];
	let y = maze[0].length - 1;
	while (y >= 0) {
		if (y % 2) {
			next_x = seed[++s];
			for (let i = Math.min(x, next_x); i <= Math.max(x, next_x); i++) {
				maze[i][y] = true;
			}
			x = next_x;
		} else {
			maze[x][y] = true;
		}
		y--;
	}
	start_pos = new Point(seed[0], maze_height - 1);
	end_pos = new Point(seed[seed.length - 1], 0);
	return maze;
}

function makeSeed() {
	seed = new Array(path_turns);
	seed[0] = randRange(1, maze_width - 1); // 1 lower bound because maze cannot start on far west tile
	for (let i = 1; i < seed.length; i++) {
		seed[i] = randRange(Math.max(seed[i-1] - max_x_change, 0), Math.min(seed[i-1] + max_x_change, maze_width - 1));
	}
}

function makeMaze() {
	makeSeed();
	return makeSeededMaze(seed);
}

function connectPoints(points, color, pathwidth) {
	ctx.beginPath();
	for (let i = 0; i < points.length - 1; i++) {
		ctx.moveTo(points[i].x * tile_size + tile_size / 2, points[i].y * tile_size + tile_size / 2);
		ctx.lineTo(points[i + 1].x * tile_size + tile_size / 2, points[i + 1].y * tile_size + tile_size / 2);
	}
	ctx.lineWidth = Math.round(tile_stroke * 1.5 * pathwidth);
	ctx.strokeStyle = color;
	ctx.stroke();
}

function drawstalledTiles() {
	for (let i = 0; i < stalled_tiles.length; i++) {
		drawMazeTile(stalled_tiles[i].x, stalled_tiles[i].y, color_tilestal);
	}
}

function drawPassedTiles() {
	for (let i = 0; i < path_taken.length; i++) {
		drawPathTile(path_taken[i].x, path_taken[i].y);
	}
}

function drawText() {
	ctx.textAlign = "center";
	ctx.fillStyle = "#FFFFFF";
	for (let x = 0; x < maze_width; x++) {
		for (let y = 0; y < maze_height; y++) {
			if (maze[x][y]) {
				ctx.fillText(`(${weighted_maze[x][y]})`, x*tile_size + tile_size*0.5, y*tile_size + tile_size*0.5);
			}
		}
	}
}

function drawCoords() {
	ctx.textAlign = "center";
	ctx.fillStyle = "#FFFFFF";
	for (let x = 0; x < maze_width; x++) {
		for (let y = 0; y < maze_height; y++) {
			if (maze[x][y]) {
				ctx.fillText(`(${x}, ${y})`, x*tile_size + tile_size*0.5, y*tile_size + tile_size*0.5);
			}
		}
	}
}

function isValidMove(current_tile, target_tile) {
	if (target_tile.x < 0 || target_tile.x > maze_width - 1) { // x overflow
		return false;
	}
	if (target_tile.y < 0 || target_tile.y > maze_height - 1) { // y overflow
		return false;
	}
	if (!maze[target_tile.x][target_tile.y]) { // tile clicked is not safe
		return false;
	}
	let move_passed_tiles = getPassedTiles(current_tile, target_tile);
	for (let i = 0; i < move_passed_tiles.length; i++) {
		if (!maze[move_passed_tiles[i].x][move_passed_tiles[i].y]) {
			return false;
		}
	}
	return true;
}

function solveMaze() {
	optimal_tickpos = new Array();
	optimal_halftickpos = new Array();
	optimal_tickpos.push(new Point(start_pos.x, start_pos.y));
	while (optimal_tickpos[optimal_tickpos.length - 1].y > 0) {
		let c = new Point(optimal_tickpos[optimal_tickpos.length - 1].x, optimal_tickpos[optimal_tickpos.length - 1].y);
		let possible_moves = [             // we only care about tiles ahead of us (i.e. not south)
			new Point(c.x - 2, c.y - 2),   // row of 5 tiles, 2 rows north of current position
			new Point(c.x - 1, c.y - 2),
			new Point(c.x, c.y - 2),
			new Point(c.x + 1, c.y - 2),
			new Point(c.x + 2, c.y - 2),
			new Point(c.x - 2, c.y - 1),   // row of 5 tiles, 1 row north of current position
			new Point(c.x - 1, c.y - 1),
			new Point(c.x, c.y - 1),
			new Point(c.x + 1, c.y - 1),
			new Point(c.x + 2, c.y - 1),
			new Point(c.x - 2, c.y),       // row of 4 tiles (not including our current position) in the current row
			new Point(c.x - 1, c.y),
			new Point(c.x + 1, c.y),
			new Point(c.x + 2, c.y)
		];
		for (let i = 0; i < possible_moves.length; i++) {
			if (!isValidMove(c, possible_moves[i])) {
				possible_moves.splice(i, 1); // remove invalid moves from array
				i--;
			}
		}
		let best_move = new Point(-1, -1);
		let best_move_score = -1;
		for (let i = 0; i < possible_moves.length; i++) {
			if (weighted_maze[possible_moves[i].x][possible_moves[i].y] > best_move_score) {
				best_move = new Point(possible_moves[i].x, possible_moves[i].y);
				best_move_score = weighted_maze[possible_moves[i].x][possible_moves[i].y];
			}
		}
		optimal_tickpos.push(new Point(best_move.x, best_move.y));
		let move_halftick = getPassedTiles(optimal_tickpos[optimal_tickpos.length - 2], optimal_tickpos[optimal_tickpos.length - 1]);
		for (let i = 0; i < move_halftick.length; i++) {
			optimal_halftickpos.push(move_halftick[i]);
		}
		optimal_halftickpos.unshift(start_pos);
	}
}

function drawScore() {
	let buffer = tile_size * 0.5;
	let text_x = 0 + buffer;
	if (seed[path_turns - 1] < maze_width / 2) {
		text_x = maze_width * tile_size - buffer;
		ctx.textAlign = "end";
	} else {
		ctx.textAlign = "start";
	}

	let strYourPath = `Your path: ${moves.length}`;
	if (stalled_tiles.length > 0) {
		strYourPath += `+${stalled_tiles.length} stalled`
	}
	let strComputerPath = `Optimal path: ${optimal_tickpos.length}`;
	ctx.strokeStyle = "black";
	ctx.fillStyle = color_lineplay;
	ctx.strokeText(strYourPath, text_x, solv_fontsize * 1.5);
	ctx.fillText(strYourPath, text_x, solv_fontsize * 1.5);
	ctx.fillStyle = color_linesolv;
	ctx.strokeText(strComputerPath, text_x, solv_fontsize * 3);
	ctx.fillText(strComputerPath, text_x, solv_fontsize * 3);
}

function drawEndGame() {
	connectPoints(optimal_halftickpos, color_linesolv, 1);
	connectPoints(path_taken, color_lineplay, 1);
	drawPassedTiles();
	drawSolutionMoves();
	drawUserMoves();
	drawScore();
}

function drawSolutionMoves() {
	ctx.lineWidth = solv_fontsize/3;
	ctx.textAlign = "center";
	ctx.fillStyle = color_linesolv;
	ctx.strokeStyle = "black";
	ctx.font = `bold ${solv_fontsize}px ${solv_font}`;
	for (let i = 0; i < optimal_tickpos.length; i++) {
		ctx.strokeText(`${i+1}`, optimal_tickpos[i].x*tile_size + tile_size*0.5, optimal_tickpos[i].y*tile_size + offset_optimal + tile_size*0.5 + solv_fontsize*0.3);
		ctx.fillText(`${i+1}`, optimal_tickpos[i].x*tile_size + tile_size*0.5, optimal_tickpos[i].y*tile_size + offset_optimal + tile_size*0.5 + solv_fontsize*0.3);
	}
}

function drawUserMoves() {
	ctx.lineWidth = solv_fontsize/3;
	ctx.textAlign = "center";
	ctx.fillStyle = color_lineplay;
	ctx.strokeStyle = "black";
	ctx.font = `bold ${solv_fontsize}px ${solv_font}`;
	for (let i = 0; i < moves.length; i++) {
		ctx.strokeText(`${i+1}`, moves[i].x*tile_size + tile_size*0.5, moves[i].y*tile_size + offset_user + tile_size*0.5 + solv_fontsize*0.3);
		ctx.fillText(`${i+1}`, moves[i].x*tile_size + tile_size*0.5, moves[i].y*tile_size + offset_user + tile_size*0.5 + solv_fontsize*0.3);
	}
}

function getNextPathTile(c_pos, o_pos) {
	let neighbors = Array(4);
	neighbors[0] = new Point(c_pos.x, c_pos.y + 1);
	neighbors[1] = new Point(c_pos.x + 1, c_pos.y);
	neighbors[2] = new Point(c_pos.x, c_pos.y - 1);
	neighbors[3] = new Point(c_pos.x - 1, c_pos.y);
	for (let i = 0; i < neighbors.length; i++) {
		if (neighbors[i].x < 0 || neighbors[i].x >= maze_width) {
			continue;
		}
		if (neighbors[i].y < 0 || neighbors[i].y >= maze_height){
			continue;
		}
		if (maze[neighbors[i].x][neighbors[i].y] == false) {
			continue;
		}
		if (o_pos.x == neighbors[i].x && o_pos.y == neighbors[i].y) {
			continue;
		}
		return neighbors[i];
	}
	return null;
}

function editSeed() {
	let savestate = prompt("Enter a seed", seed.join(" "));
	if (!savestate) {
		return;
	}
	savestate = savestate.split(' ').map(Number);
	if (savestate.length != path_turns || Math.max(...savestate) >= maze_width || Math.min(...savestate) < 0) {
		alert("Bad seed");
		return;
	}
	seed = savestate;
	reset();
}

function getPassedTiles(previous, target) {
	let current = new Point(previous.x, previous.y);
	let result = new Array();
	while (result.length < 2 && !(current.x == target.x && current.y == target.y)) {
		let movement_vector = new Point(target.x - current.x, target.y - current.y);
		if (Math.abs(movement_vector.x) == Math.abs(movement_vector.y)) { // diagonal
			current.x += (current.x < target.x ? 1 : -1);
			current.y += (current.y < target.y ? 1 : -1);
		} else if (Math.abs(movement_vector.x) > Math.abs(movement_vector.y)) {
			current.x += (current.x < target.x ? 1 : -1);
		} else {
			current.y += (current.y < target.y ? 1 : -1);
		}
		result.push(new Point(current.x, current.y));
	}
	return result;
}

canvas.addEventListener('mousedown', function (event) {
	if (moves.length == 0 && !session_active) {
		session_active = true;
		player_position = new Point(start_pos.x, start_pos.y + 1); // start off-screen, 1 tile below first maze tile
		timerTick = setInterval(gameTick, tick_length);
	}
	if (player_position.y <= 0 || (player_position.x == tornado_position.x && player_position.y == tornado_position.y)) {
		return;
	}
	let clickedTile = getTileClicked(event);
	targeted_tile = new Point(clickedTile.x, clickedTile.y);
	drawState();
});

function drawMoves() {
	for (let i = 0; i < moves.length; i++) {
		drawMoveTile(moves[i].x, moves[i].y);
	}
}

function drawTornado() {
	ctx.drawImage(imgTornado, tornado_position.x*tile_size+tile_size*0.1, tornado_position.y*tile_size+tile_size*0.1, tile_size*0.8, tile_size*0.8);
}

function drawState() {
	drawMaze();
	drawstalledTiles();
	drawPassedTiles();
	drawMoves();
	if (!(player_position.x == targeted_tile.x && player_position.y == targeted_tile.y) && player_position.y != 0) {
		drawTargetTile();
	}
	drawTornado();
	if (player_position.y <= 0) {
		drawEndGame();
	}
}

function showSolution() {
	drawMaze();
	drawstalledTiles();
	drawPassedTiles();
	drawMoves();
	drawEndGame();
}

function writePar() {
	document.getElementById("par").innerHTML = `Best possible time: ${(optimal_tickpos.length * tick_length/1000).toFixed(1)} seconds (${optimal_tickpos.length} ticks)`;
}

function writeTime() {
	let timerMsg = `${(ticks * tick_length/1000).toFixed(1)} seconds (${ticks} ticks, ${ticks_stalled} stalled)`;
	if (moves.length > 0 && moves[0].y != maze_height - 1) {
		timerMsg += " but you skipped the first tile";
		if (team_damaged) {
			timerMsg += " and you damaged your team";
		}
		timerMsg += "!";
	} else if (team_damaged) {
		timerMsg += " but you damaged your team!";
	}

	document.getElementById("timer").innerHTML = timerMsg;
}

function gameTick() {
	if (tornado_active || player_position.y <= maze_height - tornado_row) {
		tornado_active = true;
		tornado_position = path_coordinates.shift();
	}
	if ((player_position.x == targeted_tile.x && player_position.y == targeted_tile.y)) {
		ticks_stalled += 1;
		stalled_tiles.push(new Point(player_position.x, player_position.y));
	}
	ticks += 1;
	let new_tiles = getPassedTiles(player_position, targeted_tile);
	for (let i = 0; i < new_tiles.length; i++) {
		path_taken.push(new_tiles[i]);
	}
	if (new_tiles.length > 0) {
		moves.push(new_tiles[new_tiles.length - 1]);
	}
	player_position = new Point(path_taken[path_taken.length - 1].x, path_taken[path_taken.length - 1].y);
	drawState();
	if (player_position.y <= 0 || (player_position.x == tornado_position.x && player_position.y == tornado_position.y)) {
		session_active = false;
		clearInterval(timerTick);
	}
	writeTime();
	
	// Testing time between ticks (on my PC varies from 590-610 ms, which is actually better than OSRS servers)
	// if (time_a) {
	// 	time_b = performance.now();
	// 	console.log(time_b - time_a);
	// 	time_a = performance.now();
	// } else {
	// 	time_a = performance.now();
	// }
}

function runStats(amount) {
	let ticksStats = {};
	let start = performance.now();
	for (let i = 0; i < amount; i++) {
		newSession();
		if (ticksStats[optimal_tickpos.length]) {
			ticksStats[optimal_tickpos.length] += 1;
		} else {
			ticksStats[optimal_tickpos.length] = 1;
		}
	}
	let end = performance.now();
	let strStats = `Results from ${amount} mazes (${end - start} ms):`;
	for (let result in ticksStats) {
		strStats += `\n${result} ticks: ${ticksStats[result]}`;
	}
	alert(strStats);
}

function resetvars() {
	tornado_active = false;
	team_damaged = false;
	ticks = 0;
	ticks_stalled = 0;
	stalled_tiles = new Array();
	session_active = false;
	clearInterval(timerTick);
	moves = new Array();
	tornado_position = new Point();
	player_position = new Point();
	targeted_tile = new Point();
	path_taken = new Array();
	path_coordinates = new Array();
}

function newSession() {
	resetvars();
	maze = makeMaze();
	pathWeighting();
	solveMaze();
	drawMaze(maze);
	writePar();
	writeTime();
}

function reset() {
	resetvars();
	maze = makeSeededMaze(seed);
	pathWeighting();
	solveMaze();
	drawMaze(maze);
	writePar();
	writeTime();
}

var tornado_position;
var tornado_active;
var team_damaged;
var start_pos;
var end_pos;
var path_coordinates;
var ticks;
var ticks_stalled;
var stalled_tiles;
var timerTick;
var session_active;
var seed;
var maze;
var weighted_maze;
var moves;
var optimal_tickpos;
var optimal_halftickpos;
var player_position;
var targeted_tile;
var path_taken;

// var time_a;
// var time_b;

newSession();
resize();
