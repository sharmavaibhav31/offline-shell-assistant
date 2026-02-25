# 🧠 Offline AI Assistant Shell: Secure Local LLM Integration
> A **secure, offline AI assistant** integrated directly into a custom C shell — powered by [`llama.cpp`](https://github.com/ggerganov/llama.cpp) for local inference.

---

## 🌱 Project Story  

Around six months ago — I began working on something similar to the Gemini CLI agent but with a different philosophy.  

After switching to **Kali Linux**, I found even simple tasks challenging at first. Like most developers, I often relied on ChatGPT or online resources for help. Then a thought struck me:  

> “If AI assistants can guide us everywhere online, why can’t one live inside the terminal itself?”  

That question led me to explore how an AI model could safely assist users within a **command-line environment**. I quickly realized this came with serious risks — from **data corruption** and **privilege escalation** to **privacy breaches** and **system compromise**.  

Instead of abandoning the idea, I decided to design it **responsibly**, focusing on **security-first principles**:  
- Studying **AI agent behavior** inside operating systems  
- Implementing **sandboxing** and **syscall filtering (Seccomp)**  
- Ensuring **fully offline**, **local inference** for data privacy  

---
---

## 📺 Demo Video

Check out a quick demonstration of the offline AI assistant in action.

[![Watch the Demo: Offline AI Assistant Shell](https://img.youtube.com/vi/nCAE7SovqPM/maxresdefault.jpg)](https://www.youtube.com/watch?v=nCAE7SovqPM)

---

## ⚙️ Tech Stack & Security Architecture  

| Component | Purpose |
|------------|----------|
| **C** | System-level shell implementation |
| **CMake** | Build automation |
| **llama.cpp** | Efficient local inference on CPU |
| **TinyLlama-1.1B-Chat** | Lightweight conversational LLM |
| **Seccomp (Secure Computing Mode)** | Syscall filtering and sandboxing |

This architecture ensures the assistant operates **entirely offline**, in a **restricted and isolated** environment — minimizing system exposure while preserving performance and privacy.

---

## 🧠 How It Works  

Once launched, the shell initializes a **sandboxed AI worker** that processes natural language queries locally.  

mini-shell> assist what is recursion
The assistant interprets your question, generates a conversational response, and displays it — without internet access or external APIs.
Every system call is filtered through Seccomp, preventing unsafe or unauthorized operations.

### 🔐 Security Design
Layer	Mechanism	Purpose   
Seccomp	Syscall whitelist	Restrict risky system operations   
Rlimits	Resource caps	Limit CPU & memory usage    
Namespaces	Process & FS isolation	Contain execution context   
Capability Dropping	Kernel privilege reduction	Prevent escalations   

These combined mechanisms ensure strong process isolation, privacy, and fail-safe execution.

⚡ Installation
🧩 Prerequisites
sudo apt-get install build-essential cmake libcap-dev valgrind  

1️⃣ Clone & Build llama.cpp

cd ~   
git clone https://github.com/ggerganov/llama.cpp.git  
cd llama.cpp  
rm -rf build && mkdir build && cd build  
cmake .. -DBUILD_SHARED_LIBS=OFF  
make llama -j$(nproc)  
  

2️⃣ Download Model

mkdir -p ~/.assist_ai  
cd ~/.assist_ai  
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf -O model.gguf  
md5sum model.gguf > model.md5  


3️⃣ Build the Shell

cd ~/Documents/C_project/mini-shell    
mkdir build && cd build    
cmake ..    
make -j$(nproc)    


💡 Usage
Action	Command  
Run shell	./myshell  
Ask the AI assistant	mini-shell> assist what is recursion 
Reset the AI worker	mini-shell> assist-reset  
Exit the shell	mini-shell> exit  


🧪 Testing  

| Test	| Command	 | Purpose | 
|------------|----------|------------|
| Run test suite	| ./test_assist.sh	| Functional testing |  
| Verify model integrity	| ./verify_model.sh	| MD5 hash check |  
| Stress test |	./stress_test.sh |	Load/stability validation  |
| Security audit	| strace -f ./myshell	| Syscall inspection  |

⚠️ Troubleshooting
| Problem	| Possible Cause | Solution |
|------------|----------|--------------|
| Worker fails to start	|                             Missing model file	        |                  Ensure ~/.assist_ai/model.gguf exists |
| Timeout errors	        |                           Context too large	             |             Reduce n_ctx or use smaller model |
| Namespace errors	       |                          Kernel limitations	            |              Fallback disables isolation |
| Seccomp kills worker	   |                          Blocked syscall	                |            Update seccomp_filter.c whitelist |


📊 Performance (CPU-Only)
| Metric | Value | Notes |
|------------|----------|--------------|
| First query | ~2–3 s | Includes model load time |
| Subsequent queries | ~1–2 s | Reuses context |
| Memory usage | ~1–1.5 GB | Depends on model |
| Token speed |10–20 tokens/s | CPU-dependent |


🧩 Future Scope
This project represents an early exploration into secure, agentic AI at the system level.
In the next phase, I aim to:

🔁 Fine-tune the AI assistant for contextual awareness and adaptive command guidance  
🧠 Transform the shell into an agentic CLI, capable of decision-making within secure bounds  
🧩 Collaborate with mentors or security experts to enhance kernel-level safety and inference reliability  
🔒 Explore reinforcement-based task execution within isolated sandboxes  

I believe the fusion of Machine Learning and Cybersecurity can define the next generation of secure AI agents, and this project is my step toward that direction.

🛡️ Design Philosophy
“Local intelligence should be private, safe, and performant.”  
This shell demonstrates how AI inference can be embedded directly into system utilities while preserving offline privacy and integrity — valuable for embedded, defense, and research systems.  


📜 License
Licensed under the MIT License — see the LICENSE file for details.  

🤝 Contributing  
Contributions and security improvements are welcome!  
Please open an issue before submitting major changes.  

🙏 Acknowledgments  
llama.cpp — Local LLM engine  
TheBloke — Model provider    
Research inspirations from secure AI sandboxing and trusted computing  

<p align="center"><sub>🧠 Built for privacy. Designed for performance. Secured by sandboxing.</sub></p> 
