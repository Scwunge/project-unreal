#include "McPerf.h"
#include <atomic>
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

namespace McPerf
{
	bool bOn = false;

	namespace
	{
		const TCHAR* const ScopeNames[] = { TEXT("stream"), TEXT("sadd"), TEXT("sdeco"), TEXT("sunload"), TEXT("sim"), TEXT("ents"), TEXT("launch"), TEXT("upload"), TEXT("resort"), TEXT("texanim"), TEXT("hud"), TEXT("pawn"), TEXT("entrender"), TEXT("sky") };
		static_assert(UE_ARRAY_COUNT(ScopeNames) == static_cast<int32>(EMcPerfScope::Count));

		// Frames in this phase, in milliseconds, for its percentiles.
		TArray<float> PhaseFrames;
		FString Phase = TEXT("start");
		double PhaseBegan = 0.0;
		int32 PhaseOver33 = 0, PhaseOver50 = 0, PhaseOver100 = 0, PhaseOver250 = 0;

		double LastFrame = 0.0;
		double SecondBegan = 0.0;
		int32 SecondFrames = 0;
		float SecondMax = 0.0f;
		double SecondSum = 0.0;
		float SecondGame = 0.0f, SecondRender = 0.0f, SecondRhi = 0.0f, SecondGpu = 0.0f;
		float LastGame = 0.0f, LastRender = 0.0f, LastRhi = 0.0f, LastGpu = 0.0f;
		std::atomic<uint64> ScopeCycles[static_cast<int32>(EMcPerfScope::Count)];
		uint64 SecondScopeCycles[static_cast<int32>(EMcPerfScope::Count)] = {};
		int64 Counters[static_cast<int32>(EMcPerfCounter::Count)] = {};
		double GcBegan = 0.0;
		double FrameGcMs = 0.0;
		std::atomic<uint64> FramesDone{ 0 };

#if PLATFORM_WINDOWS
		HANDLE File = INVALID_HANDLE_VALUE;
		HANDLE GameThread = nullptr;

		void WriteRaw(const char* Text, int32 Length)
		{
			DWORD Written = 0;
			::WriteFile(File, Text, static_cast<DWORD>(Length), &Written, nullptr);
		}

		void WriteLine(const FString& Line)
		{
			if (File != INVALID_HANDLE_VALUE)
			{
				const FTCHARToUTF8 Utf8(*(Line + TEXT("\n")));
				WriteRaw(Utf8.Get(), Utf8.Length());
			}
		}

		// From here to the end of the namespace nothing allocates: these run in a crash, or with the game thread stopped.
		void WriteStack(CONTEXT Context)
		{
			char Line[512];
			char Module[260];
			for (int32 Depth = 0; Depth < 48 && Context.Rip; ++Depth)
			{
				DWORD64 ImageBase = 0;
				PRUNTIME_FUNCTION Function = ::RtlLookupFunctionEntry(Context.Rip, &ImageBase, nullptr);
				const char* Name = "?";
				if (ImageBase && ::GetModuleFileNameA(reinterpret_cast<HMODULE>(ImageBase), Module, sizeof(Module)))
				{
					Name = Module;
					for (const char* At = Module; *At; ++At)
					{
						if (*At == '\\' || *At == '/')
						{
							Name = At + 1;
						}
					}
				}
				const int32 Length = FCStringAnsi::Snprintf(Line, sizeof(Line), "X   #%d %s+0x%llx\n", Depth, Name, static_cast<unsigned long long>(Context.Rip - ImageBase));
				WriteRaw(Line, Length);
				if (Function)
				{
					PVOID Handler = nullptr;
					DWORD64 Frame = 0;
					::RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Context.Rip, Function, &Context, &Handler, &Frame, nullptr);
				}
				else
				{
					// A leaf function: the return address is on top of the stack.
					Context.Rip = *reinterpret_cast<DWORD64*>(Context.Rsp);
					Context.Rsp += 8;
				}
			}
		}

		LONG CALLBACK OnException(PEXCEPTION_POINTERS Info)
		{
			const DWORD Code = Info->ExceptionRecord->ExceptionCode;
			// The ones that pass through a healthy program: naming a thread, C++ throw, debug prints; and RPC's small codes,
			// but not Unreal's own assert and GPU crash codes, 0x4000 and up.
			const bool bUnreal = Code >= 0x4000 && Code <= 0x400F;
			if (!bUnreal && (Code < 0x40000000 || Code == 0x406D1388 || Code == 0xE06D7363 || Code == 0x40010006 || Code == 0x4001000A))
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}
			static std::atomic<int32> Seen{ 0 };
			if (Seen.fetch_add(1) < 6 && File != INVALID_HANDLE_VALUE)
			{
				char Line[200];
				ULONG_PTR Kind = 0, Where = 0;
				if (Info->ExceptionRecord->NumberParameters >= 2)
				{
					Kind = Info->ExceptionRecord->ExceptionInformation[0];
					Where = Info->ExceptionRecord->ExceptionInformation[1];
				}
				const int32 Length = FCStringAnsi::Snprintf(Line, sizeof(Line), "X EXCEPTION code=0x%08lx thread=%lu game_thread=%d kind=%llu addr=0x%llx\n", Code, ::GetCurrentThreadId(),
					::GetCurrentThreadId() == ::GetThreadId(GameThread) ? 1 : 0, static_cast<unsigned long long>(Kind), static_cast<unsigned long long>(Where));
				WriteRaw(Line, Length);
				WriteStack(*Info->ContextRecord);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// Watches for the game thread stopping: after this long with no frame its stack is written, and again each minute.
		DWORD WINAPI Watchdog(LPVOID)
		{
			uint64 LastSeen = 0;
			int32 StillFor = 0;
			for (;;)
			{
				::Sleep(5000);
				const uint64 Frames = FramesDone.load();
				StillFor = Frames == LastSeen ? StillFor + 5 : 0;
				LastSeen = Frames;
				if (StillFor >= 30 && StillFor % 60 == 30)
				{
					char Line[120];
					const int32 Length = FCStringAnsi::Snprintf(Line, sizeof(Line), "X STALL no frame for %d s, frame %llu\n", StillFor, static_cast<unsigned long long>(Frames));
					WriteRaw(Line, Length);
					if (::SuspendThread(GameThread) != static_cast<DWORD>(-1))
					{
						CONTEXT Context = {};
						Context.ContextFlags = CONTEXT_FULL;
						if (::GetThreadContext(GameThread, &Context))
						{
							WriteStack(Context);
						}
						::ResumeThread(GameThread);
					}
				}
			}
		}
#else
		void WriteLine(const FString&) {}
#endif

		double Now()
		{
			return FPlatformTime::Seconds() - GStartTime;
		}

		void WritePhaseSummary()
		{
			if (PhaseFrames.Num() == 0)
			{
				return;
			}
			PhaseFrames.Sort();
			const int32 N = PhaseFrames.Num();
			double Sum = 0.0;
			for (const float Ms : PhaseFrames)
			{
				Sum += Ms;
			}
			// "1% low": the frame rate of the slowest 1% of frames taken together, and so for 0.1%.
			auto Low = [&](double Fraction)
			{
				const int32 Count = FMath::Max(1, FMath::FloorToInt32(N * Fraction));
				double Slowest = 0.0;
				for (int32 Index = N - Count; Index < N; ++Index)
				{
					Slowest += PhaseFrames[Index];
				}
				return Slowest > 0.0 ? 1000.0 * Count / Slowest : 0.0;
			};
			WriteLine(FString::Printf(TEXT("P phase=%s secs=%.1f n=%d fps=%.1f p50=%.1f p95=%.1f p99=%.1f low1=%.1f low01=%.1f max=%.1f over33=%d over50=%d over100=%d over250=%d"),
				*Phase, Now() - PhaseBegan, N, Sum > 0.0 ? 1000.0 * N / Sum : 0.0, PhaseFrames[N / 2], PhaseFrames[FMath::Min(N - 1, N * 95 / 100)], PhaseFrames[FMath::Min(N - 1, N * 99 / 100)],
				Low(0.01), Low(0.001), PhaseFrames[N - 1], PhaseOver33, PhaseOver50, PhaseOver100, PhaseOver250));
		}

		void OnEndFrame()
		{
			const double Time = FPlatformTime::Seconds();
			FramesDone.fetch_add(1);
			if (LastFrame == 0.0)
			{
				LastFrame = SecondBegan = Time;
				return;
			}
			const float Ms = static_cast<float>((Time - LastFrame) * 1000.0);
			LastFrame = Time;
			PhaseFrames.Add(Ms);
			PhaseOver33 += Ms > 33.4f;
			PhaseOver50 += Ms > 50.0f;
			PhaseOver100 += Ms > 100.0f;
			PhaseOver250 += Ms > 250.0f;
			++SecondFrames;
			SecondSum += Ms;
			SecondMax = FMath::Max(SecondMax, Ms);
			SecondGame += LastGame;
			SecondRender += LastRender;
			SecondRhi += LastRhi;
			SecondGpu += LastGpu;

			const double ToMs = FPlatformTime::GetSecondsPerCycle64() * 1000.0;
			uint64 Frame[static_cast<int32>(EMcPerfScope::Count)];
			for (int32 Index = 0; Index < static_cast<int32>(EMcPerfScope::Count); ++Index)
			{
				Frame[Index] = ScopeCycles[Index].exchange(0);
				SecondScopeCycles[Index] += Frame[Index];
			}
			if (Ms > 33.4f)
			{
				FString Line = FString::Printf(TEXT("H t=%.2f ms=%.1f gt=%.1f rt=%.1f rhi=%.1f gpu=%.1f gc=%.1f dirty=%lld flight=%lld"), Now(), Ms, LastGame, LastRender, LastRhi, LastGpu, FrameGcMs,
					Counters[static_cast<int32>(EMcPerfCounter::Dirty)], Counters[static_cast<int32>(EMcPerfCounter::InFlight)]);
				for (int32 Index = 0; Index < static_cast<int32>(EMcPerfScope::Count); ++Index)
				{
					const double ScopeMs = Frame[Index] * ToMs;
					if (ScopeMs >= 1.0)
					{
						Line += FString::Printf(TEXT(" %s=%.1f"), ScopeNames[Index], ScopeMs);
					}
				}
				WriteLine(Line);
			}
			FrameGcMs = 0.0;

			if (Time - SecondBegan >= 1.0)
			{
				const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
				const float N = FMath::Max(1, SecondFrames);
				auto Counter = [](EMcPerfCounter Which) { return Counters[static_cast<int32>(Which)]; };
				FString Line = FString::Printf(TEXT("S t=%.1f phase=%s n=%d avg=%.1f max=%.1f gt=%.1f rt=%.1f rhi=%.1f gpu=%.1f rss=%d peak=%d commit=%d chunks=%lld dirty=%lld flight=%lld meshes=%lld water=%lld ktris=%lld ents=%lld pos=%lld,%lld,%lld lverts=%lld ltris=%lld kept=%lld uobj=%lld"),
					Now(), *Phase, SecondFrames, SecondSum / N, SecondMax, SecondGame / N, SecondRender / N, SecondRhi / N, SecondGpu / N,
					static_cast<int32>(Memory.UsedPhysical / (1024 * 1024)), static_cast<int32>(Memory.PeakUsedPhysical / (1024 * 1024)), static_cast<int32>(Memory.UsedVirtual / (1024 * 1024)),
					Counter(EMcPerfCounter::Chunks), Counter(EMcPerfCounter::Dirty), Counter(EMcPerfCounter::InFlight), Counter(EMcPerfCounter::Meshes), Counter(EMcPerfCounter::WaterMeshes),
					Counter(EMcPerfCounter::MeshTris), Counter(EMcPerfCounter::Entities), Counter(EMcPerfCounter::X), Counter(EMcPerfCounter::Y), Counter(EMcPerfCounter::Z),
					Counter(EMcPerfCounter::LiveKVerts), Counter(EMcPerfCounter::LiveKTris), Counter(EMcPerfCounter::KeptKVerts), Counter(EMcPerfCounter::UObjects));
				for (int32 Index = 0; Index < static_cast<int32>(EMcPerfScope::Count); ++Index)
				{
					Line += FString::Printf(TEXT(" %s=%.2f"), ScopeNames[Index], SecondScopeCycles[Index] * ToMs / N);
					SecondScopeCycles[Index] = 0;
				}
				WriteLine(Line);
				SecondBegan = Time;
				SecondFrames = 0;
				SecondSum = 0.0;
				SecondMax = 0.0f;
				SecondGame = SecondRender = SecondRhi = SecondGpu = 0.0f;
			}
		}
	}

	bool IsOn()
	{
		return bOn;
	}

	void Start()
	{
#if PLATFORM_WINDOWS
		FString Path;
		if (bOn || !FParse::Value(FCommandLine::Get(), TEXT("-McPerf="), Path))
		{
			return;
		}
		Path.TrimQuotesInline();
		File = ::CreateFileW(*Path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (File == INVALID_HANDLE_VALUE)
		{
			return;
		}
		::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &GameThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
		::AddVectoredExceptionHandler(1, OnException);
		::CreateThread(nullptr, 64 * 1024, Watchdog, nullptr, 0, nullptr);
		bOn = true;
		PhaseFrames.Reserve(64 * 1024);
		PhaseBegan = Now();
		WriteLine(FString::Printf(TEXT("E t=%.2f START cmd=%s"), Now(), FCommandLine::Get()));
		FCoreDelegates::OnEndFrame.AddStatic(&OnEndFrame);
		FCoreDelegates::OnEnginePreExit.AddLambda([]
		{
			WritePhaseSummary();
			PhaseFrames.Reset();
			WriteLine(FString::Printf(TEXT("E t=%.2f CLEAN_EXIT"), Now()));
		});
#endif
	}

	void Mark(const FString& NewPhase)
	{
		if (!bOn)
		{
			return;
		}
		WritePhaseSummary();
		PhaseFrames.Reset();
		PhaseOver33 = PhaseOver50 = PhaseOver100 = PhaseOver250 = 0;
		Phase = NewPhase;
		PhaseBegan = Now();
		WriteLine(FString::Printf(TEXT("E t=%.2f PHASE %s"), Now(), *NewPhase));
	}

	void Event(const FString& Text)
	{
		if (bOn)
		{
			WriteLine(FString::Printf(TEXT("E t=%.2f %s"), Now(), *Text));
		}
	}

	void SetCounter(EMcPerfCounter Counter, int64 Value)
	{
		Counters[static_cast<int32>(Counter)] = Value;
	}

	int64 GetCounter(EMcPerfCounter Counter)
	{
		return Counters[static_cast<int32>(Counter)];
	}

	void SetFrameTimes(float GameMs, float RenderMs, float RhiMs, float GpuMs)
	{
		LastGame = GameMs;
		LastRender = RenderMs;
		LastRhi = RhiMs;
		LastGpu = GpuMs;
	}

	void AddScopeCycles(EMcPerfScope Scope, uint64 Cycles)
	{
		ScopeCycles[static_cast<int32>(Scope)].fetch_add(Cycles, std::memory_order_relaxed);
	}

	void GcBegin()
	{
		GcBegan = FPlatformTime::Seconds();
	}

	void GcEnd()
	{
		if (bOn)
		{
			const double Ms = (FPlatformTime::Seconds() - GcBegan) * 1000.0;
			FrameGcMs += Ms;
			WriteLine(FString::Printf(TEXT("E t=%.2f GC %.1f ms"), Now(), Ms));
		}
	}
}

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
