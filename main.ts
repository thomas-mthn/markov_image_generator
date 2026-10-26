"use strict";

let canvas = document.getElementById("canvas") as HTMLCanvasElement;
let canvas_context = canvas.getContext("2d");

let wasm_memory;
let g_train;
let g_generate;

let wasmTrain: (width: number,height: number,mipmap: number,offset: number,amount: number) => void;
let wasmTrainInit: (width: number,height: number,quality: number) => void;
let wasmGenerate: (size: number,mipmap: number,offset: number,amount: number) => void;
let wasmGenerateInit: (quality: number,size: number,mipmap: number) => void;
let wasmGenerateMipmapInit: (width: number,height: number,mipmap: number) => void;
let wasmRemixInit: (quality: number,size: number) => void;
let wasmInpaint: (width: number,height: number,mipmap: number,offset: number,amount: number) => void;
let wasmInpaintInit: (quality: number,width: number,height: number,mipmap: number) => void;

let g_vram;
let g_image;

let camera_x = 0;
let camera_y = 0;
let camera_zoom = 1;

let g_rerender = false;

function transformX(position: number): number{
	return position * g_image.width * camera_zoom / canvas.width + camera_x;
}

function transformY(position: number): number{
	return position * g_image.width * camera_zoom / canvas.width + camera_y;
}

function mipmapOffset(width: number,height: number,mipmap: number): number{
	let offset = 0;
	for(let i = 0;i < mipmap;i++)
		offset += (width >> i) * (height >> i);
	return offset;
}

function renderImage(image: ImageData,size: number,offset: number){
	for(let i = 0;i < canvas.height * canvas.width;i++){
		g_vram.data[i * 4 + 0] = 0x80;
		g_vram.data[i * 4 + 1] = 0x80;
		g_vram.data[i * 4 + 2] = 0x80;
		g_vram.data[i * 4 + 3] = 0xFF;
	}
	if((document.getElementById("checkbox_inpaint") as HTMLInputElement).checked){
		for(let i = 0;i < canvas.height * canvas.width;i++){
			let x = Math.floor(transformX(i / canvas.width));
			let y = Math.floor(transformY(i % canvas.width));
			if(x < 0 || x >= size || y < 0 || y >= size)
				continue;
			if(image.data[(offset + x * size + y) * 4 + 3] != 0)
				continue;
			g_vram.data[i * 4 + 0] = image.data[(offset + x * size + y) * 4 + 0];
			g_vram.data[i * 4 + 1] = image.data[(offset + x * size + y) * 4 + 1]; 
			g_vram.data[i * 4 + 2] = image.data[(offset + x * size + y) * 4 + 2]; 	 
		}
	}
	else{
		for(let i = 0;i < canvas.height;i++){
			let x = Math.floor(transformX(i));
			if(x < 0 || x >= size)
				continue;
			let value_1 = transformY(0);
			let value_2 = transformY(canvas.width - 1);
			let delta = (value_2 - value_1) / canvas.width;
			
			let index = (i * canvas.width) * 4;
			let width = canvas.width;
			for(let j = 0;j < width;j++){
				let y = Math.floor(value_1);
				value_1 += delta;

				if(y >= 0 && y < size){
					let index_image = (offset + x * size + y) * 4;
					
					g_vram.data[index + 0] = image.data[index_image + 0];
					g_vram.data[index + 1] = image.data[index_image + 1]; 
					g_vram.data[index + 2] = image.data[index_image + 2];
					g_vram.data[index + 3] = 0xFF;
				}
				index += 4;
			}
		}
	}
	canvas_context.putImageData(g_vram,0,0);
}

function renderImageRaw(image: Int32Array,width: number,height: number,offset: number){
	if(!g_image || width != g_image.width || height != g_image.height)
		g_image = new ImageData(width,height);
	for(let i = 0;i < width * height;i++){
		let color = image[offset + i];
		g_image.data[i * 4 + 0] = color >> 0  & 0xFF;
		g_image.data[i * 4 + 1] = color >> 8  & 0xFF; 
		g_image.data[i * 4 + 2] = color >> 16 & 0xFF;
		g_image.data[i * 4 + 3] = 0;                  
	}
	g_rerender = true;
}

function inpaintReverse(){
	for(let i = 3;i < g_image.width * g_image.width * 4;i += 4)
		g_image.data[i] = !g_image.data[i];
	g_rerender = true;
}

async function inpaintImage(){
	let wasm_image = new Int32Array(wasm_memory.buffer,g_generate,g_image.width * g_image.height);
	for(let i = 0;i < g_image.width * g_image.width;i++){
		wasm_image[i] =  g_image.data[i * 4 + 0] << 0;
		wasm_image[i] |= g_image.data[i * 4 + 1] << 8;
		wasm_image[i] |= g_image.data[i * 4 + 2] << 16;
		wasm_image[i] |= g_image.data[i * 4 + 3] << 24;
	}
	let input_quality = document.getElementById("input_quality") as HTMLInputElement;
	let remix_mipmap = document.getElementById("remix_mipmap") as HTMLInputElement;
	wasmInpaintInit(parseInt(input_quality.value),g_image.width,g_image.height,parseInt(remix_mipmap.value));
	for(let mipmap = parseInt(remix_mipmap.value);mipmap--;){
		let mipmap_width = g_image.width >> mipmap;
		let mipmap_height = g_image.height >> mipmap;
		if(mipmap_width < 4 || mipmap_height < 4)
			continue;
		let mipmap_offset = mipmapOffset(g_image.width,g_image.height,mipmap);
		wasmGenerateMipmapInit(g_image.width,g_image.height,mipmap);
		let n_generate_sample = 1 << 14;
		for(let i = 0;i < 2;i++){
			for(let j = 0;j < mipmap_width * mipmap_height;j += n_generate_sample){
				let amount = n_generate_sample;
				if(mipmap_width * mipmap_height < n_generate_sample)
					amount = mipmap_width * mipmap_height;
				wasmInpaint(g_image.width,g_image.height,mipmap,j,amount);
				let image_generated = new Int32Array(wasm_memory.buffer,g_generate,g_image.width * g_image.height * 2);
				
				renderImageRaw(image_generated,mipmap_width,mipmap_height,mipmap_offset);
				
				(document.getElementById("progress_bar") as HTMLProgressElement).value = j / (mipmap_width * mipmap_height);
				
				await new Promise(r => requestAnimationFrame(r));
			}
		}
	}
}

async function remixImage(){
	let wasm_image = new Int32Array(wasm_memory.buffer,g_generate,g_image.width * g_image.width);
	for(let i = 0;i < g_image.width * g_image.width;i++){
		wasm_image[i] =  g_image.data[i * 4 + 0] << 0;
		wasm_image[i] |= g_image.data[i * 4 + 1] << 8;
		wasm_image[i] |= g_image.data[i * 4 + 2] << 16;
		wasm_image[i] |= g_image.data[i * 4 + 3] << 24;
	}
	let generate_size = g_image.width;
	let input_quality = document.getElementById("input_quality") as HTMLInputElement;
	let remix_mipmap = document.getElementById("remix_mipmap") as HTMLInputElement;
	wasmRemixInit(parseInt(input_quality.value),g_image.width);
	for(let mipmap = parseInt(remix_mipmap.value);mipmap--;){
		let mipmap_size = generate_size >> mipmap;
		if(mipmap_size < 4)
			continue;
		let mipmap_offset = mipmapOffset(generate_size,generate_size,mipmap);
		wasmGenerateMipmapInit(generate_size,mipmap,0);
		let n_generate_sample = 1 << 14;
		let image_generated = new Int32Array(wasm_memory.buffer,g_generate,generate_size * generate_size * 2);
		for(let i = 0;i < Math.min(16,2 << mipmap);i++){
			for(let j = 0;j < mipmap_size * mipmap_size;j += n_generate_sample){
				let amount = n_generate_sample;
				if(mipmap_size * mipmap_size < n_generate_sample)
					amount = mipmap_size * mipmap_size;
				wasmGenerate(generate_size,mipmap,j,amount);
				
				renderImageRaw(image_generated,mipmap_size,mipmap_size,mipmap_offset);
				
				(document.getElementById("progress_bar") as HTMLProgressElement).value = j / (mipmap_size * mipmap_size);
				
				await new Promise(r => requestAnimationFrame(r));
			}
		}
	}
}

async function imageOutpaint(){
	let prev_size = g_image.width;
	let size = prev_size * 2;
	let image_new = new ImageData(size,size);
	for(let i = 0;i < prev_size * g_image.height;i++){
		let x = Math.floor(i / prev_size) + prev_size / 2;
		let y = Math.floor(i % prev_size) + prev_size / 2;
		image_new.data[(x * size + y) * 4 + 0] = g_image.data[i * 4 + 0];
		image_new.data[(x * size + y) * 4 + 1] = g_image.data[i * 4 + 1];
		image_new.data[(x * size + y) * 4 + 2] = g_image.data[i * 4 + 2];
		image_new.data[(x * size + y) * 4 + 3] = 1;
	}
	g_image = image_new;
	inpaintImage();
}

async function imageGenerate(){
	let generation_size = document.getElementById("generation_size") as HTMLInputElement;
	let remix_mipmap = document.getElementById("remix_mipmap") as HTMLInputElement;
	let input_quality = document.getElementById("input_quality") as HTMLInputElement;
	let generate_size = parseInt(generation_size.value);
	console.log(remix_mipmap.value);
	wasmGenerateInit(parseInt(input_quality.value),generate_size,parseInt(remix_mipmap.value));
	for(let mipmap = parseInt(remix_mipmap.value);mipmap--;){
		let mipmap_size = generate_size >> mipmap;
		if(mipmap_size < 4)
			continue;
		let mipmap_offset = mipmapOffset(generate_size,generate_size,mipmap);
		wasmGenerateMipmapInit(generate_size,mipmap,0);
		let n_generate_sample = 1 << 14;
		for(let i = 0;i < Math.min(4,1 << mipmap);i++){
			for(let j = 0;j < mipmap_size * mipmap_size;j += n_generate_sample){
				let amount = n_generate_sample;
				if(mipmap_size * mipmap_size < n_generate_sample)
					amount = mipmap_size * mipmap_size;
				wasmGenerate(generate_size,mipmap,j,amount);
				let image_generated = new Int32Array(wasm_memory.buffer,g_generate,generate_size * generate_size * 2);
				
				renderImageRaw(image_generated,mipmap_size,mipmap_size,mipmap_offset);
				
				(document.getElementById("progress_bar") as HTMLProgressElement).value = j / (mipmap_size * mipmap_size);
				
				await new Promise(r => requestAnimationFrame(r));
			}
		}
	}
}

async function imageTrain(img: HTMLImageElement){
	for(let mipmap = 16;mipmap--;){
		let mipmap_width  = img.naturalWidth  >> mipmap;
		let mipmap_height = img.naturalHeight >> mipmap;
		let mipmap_offset = mipmapOffset(img.naturalWidth,img.naturalHeight,mipmap);
		let n_generate_sample = 1 << 11;
		for(let j = 0;j < mipmap_width * mipmap_height;j += n_generate_sample){
			let amount = n_generate_sample;
			if(mipmap_width * mipmap_height < n_generate_sample)
				amount = mipmap_width * mipmap_height;
			wasmTrain(img.naturalWidth,img.naturalHeight,mipmap,j,amount);
			
			(document.getElementById("progress_bar") as HTMLProgressElement).value = j / (mipmap_width * mipmap_height);
			
			await new Promise(r => requestAnimationFrame(r));
		}
	}
}

function inpaintToggle(){
	g_rerender = true;
}

function imageElementExtract(img: HTMLImageElement){
	let canvas_width = canvas.width;
	let canvas_height = canvas.height;
	canvas.width = img.naturalWidth;
	canvas.height = img.naturalHeight;
	
	canvas_context.drawImage(img,0,0);
	let image_data = canvas_context.getImageData(0,0,img.naturalWidth,img.naturalHeight);
	canvas.width = canvas_width;
	canvas.height = canvas_height;
	return image_data;
}

function imageLoadAndTrain(img: HTMLImageElement){
	let array = new Int32Array(wasm_memory.buffer,g_train,img.naturalWidth * img.naturalHeight);
	let image_data = imageElementExtract(img);

	for(let i = 0;i < img.naturalWidth * img.naturalHeight;i++){
		array[i] =  image_data.data[i * 4 + 0] << 0;
		array[i] |= image_data.data[i * 4 + 1] << 8;
		array[i] |= image_data.data[i * 4 + 2] << 16;
	}
	wasmTrainInit(img.naturalWidth,img.naturalHeight,16);
	imageTrain(img);
}

function imageLoad(img: HTMLImageElement){
	let image_data = imageElementExtract(img);
	g_image = new ImageData(img.naturalWidth,img.naturalHeight);
	
	g_image.data.set(image_data.data);
	g_rerender = true;
}

function imageFromServerLoadAndTrain(img: HTMLImageElement){
	let box = document.getElementById("box_server_images");
	box.replaceChildren();
	imageLoadAndTrain(img);
}

function imageFromServerLoad(img: HTMLImageElement){
	let box = document.getElementById("box_server_images");
	box.replaceChildren();
	imageLoad(img);
}

let image_train_samples = [
	"board_small.png",
	"card.png",
	"card_back.png",
	"brick24.png",
	"city.PNG",
	"maze.PNG",
	"flowers.PNG",
	"flag.PNG",
	"gogh.PNG",
	"texture_o_icon.jpg",
];

function serverLoadImagesLoad(){
	let box = document.getElementById("box_server_images");
	if(box.hasChildNodes()){
		box.replaceChildren();
	}
	else{
		for(let image_src of image_train_samples){
			let image = document.createElement("img");
			image.src = "web_img/" + image_src;
			image.width = 128;
			image.height = 128;
			image.onclick = () => imageFromServerLoad(image);
			box.appendChild(image);
		}
	}
}

function serverLoadImagesTrain(){
	let box = document.getElementById("box_server_images");
	if(box.hasChildNodes()){
		box.replaceChildren();
	}
	else{
		for(let image_src of image_train_samples){
			let image = document.createElement("img");
			image.src = "web_img/" + image_src;
			image.width = 128;
			image.height = 128;
			image.onclick = () => imageFromServerLoadAndTrain(image);
			box.appendChild(image);
		}
	}
}

document.getElementById("input_quality").addEventListener("input",(event) => {
	let quality_value = document.getElementById("input_quality_value") as HTMLInputElement;
	quality_value.innerHTML = (event.target as HTMLInputElement).value;
});

document.getElementById("generation_size").addEventListener("input",(event) => {
	let quality_value = document.getElementById("generation_size_value") as HTMLInputElement;
	quality_value.innerHTML = (event.target as HTMLInputElement).value;
});

document.getElementById("generation_size").addEventListener("change",(event) => {
	let size = parseInt((document.getElementById("generation_size") as HTMLInputElement).value);
	let image_new = new ImageData(size,size);
	for(let i = 0;i < size * size;i++){
		let x = Math.floor(i / size);
		let y = Math.floor(i % size);
		if(x >= g_image.width || y >= g_image.width)
			continue;
		image_new.data[i * 4 + 0] = g_image.data[(x * g_image.width + y) * 4 + 0];
		image_new.data[i * 4 + 1] = g_image.data[(x * g_image.width + y) * 4 + 1];
		image_new.data[i * 4 + 2] = g_image.data[(x * g_image.width + y) * 4 + 2];
		image_new.data[i * 4 + 3] = g_image.data[(x * g_image.width + y) * 4 + 3];
	}
	g_image = image_new;
	g_rerender = true;
});

document.getElementById("remix_mipmap").addEventListener("input",(event) => {
	let quality_value = document.getElementById("remix_mipmap_value");
	quality_value.innerHTML = (event.target as HTMLInputElement).value;
});

document.getElementById("canvas").addEventListener("mousemove",(event) => {
	if(!(event.buttons & 1))
		return;
	if((document.getElementById("checkbox_inpaint") as HTMLInputElement).checked)
		return;
	camera_x -= event.movementY * camera_zoom * g_image.width / canvas.width;
	camera_y -= event.movementX * camera_zoom * g_image.width / canvas.width;
	g_rerender = true;
});

let inpaint_position_x = 0;
let inpaint_position_y = 0;

document.getElementById("canvas").addEventListener("mousedown",(event) => {
	if(!(document.getElementById("checkbox_inpaint") as HTMLInputElement).checked)
		return;
	inpaint_position_x = transformX(event.offsetY);
	inpaint_position_y = transformY(event.offsetX);
});

document.getElementById("canvas").addEventListener("mouseup",(event) => {
	if(!(document.getElementById("checkbox_inpaint") as HTMLInputElement).checked)
		return;
	
	let begin_x = Math.floor(Math.min(inpaint_position_x,transformX(event.offsetY)));
	let end_x = Math.floor(Math.max(inpaint_position_x,transformX(event.offsetY)));
	let begin_y = Math.floor(Math.min(inpaint_position_y,transformY(event.offsetX)));
	let end_y = Math.floor(Math.max(inpaint_position_y,transformY(event.offsetX)));
	for(let mipmap = 16;mipmap--;){
		for(let x = begin_x >> mipmap;x < end_x >> mipmap;x++){
			for(let y = begin_y >> mipmap;y < end_y >> mipmap;y++){
				let offset = mipmapOffset(g_image.width,g_image.height,mipmap) * 4;
				let mipmap_size = g_image.width >> mipmap;
				let index = (x * mipmap_size + y) * 4;
				g_image.data[offset + index + 3] = 1;
			}
		}
	}
	g_rerender = true;
});

document.getElementById("canvas").addEventListener("wheel",(event) => {
	if(event.deltaY > 0){
		camera_zoom *= 1.1;
		camera_x -= camera_zoom * 15;
		camera_y -= camera_zoom * 15;
	}
	else{
		camera_zoom /= 1.1;
		camera_x += camera_zoom * 15;
		camera_y += camera_zoom * 15;
	}
	g_rerender = true;
});

document.getElementById("load_file_input").addEventListener("change", (event) => {
	let file = (event.target as HTMLInputElement).files[0];
	if(!file) 
		return;
	let reader = new FileReader();
	reader.onload = (e) => {
		let img = new Image();
		img.src = e.target.result as string;
		img.onload = () => {
			imageLoad(img);
		};
	};
	reader.readAsDataURL(file);
});

document.getElementById("train_file_input").addEventListener("change",(event) => {
	let file = (event.target as HTMLInputElement).files[0];
	if(!file) 
		return;
	let reader = new FileReader();
	reader.onload = (e) => {
		let img = new Image();
		img.src = e.target.result as string;
		img.onload = () => {
			imageLoadAndTrain(img);
		};
	};
	reader.readAsDataURL(file);
});

document.getElementById("server_train_local").addEventListener("change",(event) => {
	let file = (event.target as HTMLInputElement).files[0];
	if(!file) 
		return;
	let reader = new FileReader();
	reader.onload = (e) => {
		let img = new Image();
		img.src = e.target.result as string;
		img.onload = () => {
			let canvas_temp = document.createElement("canvas");
			canvas_temp.width = img.naturalWidth;
			canvas_temp.height = img.naturalHeight;

			let context = canvas_temp.getContext("2d");
			context.drawImage(img,0,0);

			canvas_temp.toBlob(blob => {
				let request = new XMLHttpRequest();
				request.open("TRAIN", "");
				request.send(blob);
			});
		};
	};
	reader.readAsDataURL(file);
	document.getElementById("server_buttons_main").style.display = "inline-block";
	document.getElementById("server_buttons_generate").style.display = "none";
});

function serverTrainCloud(){
	let box = document.getElementById("box_server_images");
	if(box.hasChildNodes()){ 
		box.replaceChildren();
	}
	else{
		for(let image_src of image_train_samples){
			let image = document.createElement("img");
			image.src = "web_img/" + image_src;
			image.width = 128;
			image.height = 128;
			image.onclick = () => {
				let canvas_temp = document.createElement("canvas");
				canvas_temp.width = image.naturalWidth;
				canvas_temp.height = image.naturalHeight;

				let context = canvas_temp.getContext("2d");
				context.drawImage(image,0,0);

				canvas_temp.toBlob(blob => {
					let request = new XMLHttpRequest();
					request.open("TRAIN", "");
					request.send("web_img/" + image_src);
					box.replaceChildren();
				});
			};
			box.appendChild(image);
		}
	}
	document.getElementById("server_buttons_main").style.display = "inline-block";
	document.getElementById("server_buttons_generate").style.display = "none";
}

function serverTrain(){
	document.getElementById("server_buttons_main").style.display = "none";
	document.getElementById("server_buttons_generate").style.display = "inline-block";
}

function modelReset(){
	let request = new XMLHttpRequest();
	request.open("TRAIN_DELETE","");
	request.send();
}

async function serverRemixImage(){
	let mipmap_start = parseInt((document.getElementById("remix_mipmap") as HTMLInputElement).value);
	let width = g_image.width;
	let height = g_image.height;
	for(let mipmap = mipmap_start;mipmap--;){
		let buffer;
		if(mipmap_start === (mipmap + 1))
			buffer = new ArrayBuffer(g_image.data.length + 16);
		else
			buffer = new ArrayBuffer(16);
		
		let view = new DataView(buffer);
		view.setInt32(0,width,true);
		view.setInt32(4,height,true);
		view.setInt32(8,mipmap,true);
		view.setInt32(12,Number(mipmap_start === (mipmap + 1)),true);
		
		if(mipmap_start === (mipmap + 1))
			new Uint8Array(buffer,16).set(g_image.data);
		
		await new Promise<void>((resolve,reject) => {
			let request = new XMLHttpRequest();
			request.open("REMIX","");
			request.responseType = "blob";
			request.onload = () => {
				let blob = request.response;
				let url = URL.createObjectURL(blob);
				console.log(url);
				let img = document.createElement('img');
				img.src = url;
				img.onload = () => {
					imageLoad(img);
					resolve();
				};
			};
			request.send(buffer);
		});
	}
}

async function serverOutpaintImage(){
	let mipmap_start = parseInt((document.getElementById("remix_mipmap") as HTMLInputElement).value);
	let width = g_image.width;
	let height = g_image.height;
	for(let mipmap = mipmap_start;mipmap--;){
		let buffer;
		if(mipmap_start === (mipmap + 1))
			buffer = new ArrayBuffer(g_image.data.length + 16);
		else
			buffer = new ArrayBuffer(16);
		
		let view = new DataView(buffer);
		view.setInt32(0,width,true);
		view.setInt32(4,height,true);
		view.setInt32(8,mipmap,true);
		view.setInt32(12,Number(mipmap_start === (mipmap + 1)),true);
		
		if(mipmap_start === (mipmap + 1))
			new Uint8Array(buffer,16).set(g_image.data);
		
		await new Promise<void>((resolve,reject) => {
			let request = new XMLHttpRequest();
			request.open("OUTPAINT","");
			request.responseType = "blob";
			request.onload = () => {
				let blob = request.response;
				let url = URL.createObjectURL(blob);
				console.log(url);
				let img = document.createElement('img');
				img.src = url;
				img.onload = () => {
					imageLoad(img);
					resolve();
				};
			};
			request.send(buffer);
		});
	}
}

async function serverGenerateImage(){
	let mipmap_start = parseInt((document.getElementById("remix_mipmap") as HTMLInputElement).value);
	for(let mipmap = mipmap_start;mipmap--;){
		let buffer = new ArrayBuffer(16);
		let view = new DataView(buffer);
		view.setInt32(0,parseInt((document.getElementById("generation_size") as HTMLInputElement).value),true);
		view.setInt32(4,parseInt((document.getElementById("input_quality") as HTMLInputElement).value),true);
		view.setInt32(8,mipmap,true);
		view.setInt32(12,Number(mipmap_start === (mipmap + 1)),true);
		await new Promise<void>((resolve,reject) => {
			let request = new XMLHttpRequest();
			request.open("GENERATE","");
			request.responseType = "blob";
			request.onload = () => {
				if(request.status == 200){
					let blob = request.response;
					let url = URL.createObjectURL(blob);
					let img = new Image();
					img.src = url;
					img.onload = () => {
						imageLoad(img);
						resolve();
					};
				}
			};
			request.send(buffer);
		});
	}
}

function imageExport(){
    let pngUrl = canvas.toDataURL("image/png");
    let link = document.createElement("a");
    link.href = pngUrl;
    link.download = "export.png";
    link.click();
}

async function getWasm(){
	let response = await fetch("main.wasm");
	let bytes = await response.arrayBuffer();
	let result = await WebAssembly.instantiate(bytes, {});
	let exports = result.instance.exports;
	wasm_memory = exports.memory;
	g_train = exports.g_train;
	g_generate = exports.g_generate;
	wasmTrainInit = exports.wasmTrainInit as any;
	wasmTrain = exports.wasmTrain as any;
	wasmGenerate = exports.wasmGenerate as any;
	wasmGenerateInit = exports.wasmGenerateInit as any;
	wasmGenerateMipmapInit = exports.wasmGenerateMipmapInit as any;
	wasmRemixInit = exports.wasmRemixInit as any;
	wasmInpaint = exports.wasmInpaint as any;
	wasmInpaintInit = exports.wasmInpaintInit as any;
}

function renderLoop(){
    if(g_rerender){
        renderImage(g_image,g_image.width,0);
        g_rerender = false;
    }
    requestAnimationFrame(renderLoop);
}

g_vram = canvas_context.createImageData(canvas.height,canvas.width);
getWasm();
renderLoop();
