struct ParticleData
{
    float3 Position;
    float LifeTime;
    float3 Velocity;
};

struct CS_INPUT
{
	uint3 GroupId : SV_GROUPID;
	uint3 GroupThreadId : SV_GROUPTHREADID;
	uint3 DispatchThreadId : SV_DISPATCHTHREADID;
	uint GroupIndex : SV_GROUPINDEX;
};

#define DEAD_LIST_COUNTER 0
#define ALIVE_LIST_1_COUNTER 4
#define ALIVE_LIST_2_COUNTER 8
#define EMIT_COUNT 12

#ifdef COMPILE_UPDATE_PARAMETERS

cbuffer Parameters : register(b0)
{
    uint cEmitCount;
}

RWByteAddressBuffer uCounters : register(u0);
RWByteAddressBuffer uEmitArguments : register(u1);
RWByteAddressBuffer uSimulateArguments : register(u2);

[numthreads(1, 1, 1)]
void UpdateSimulationParameters(CS_INPUT input)
{
    uint deadCount = uCounters.Load(DEAD_LIST_COUNTER);
    uint aliveParticleCount = uCounters.Load(ALIVE_LIST_2_COUNTER);

    uint emitCount = min(deadCount, cEmitCount);

    uEmitArguments.Store3(0, uint3(ceil((float)emitCount / 128), 1, 1));

    uint simulateCount = ceil((float)(aliveParticleCount + emitCount) / 128);
    uSimulateArguments.Store3(0, uint3(simulateCount, 1, 1));

    uCounters.Store(ALIVE_LIST_1_COUNTER, aliveParticleCount);
    uCounters.Store(ALIVE_LIST_2_COUNTER, 0);
    uCounters.Store(EMIT_COUNT, emitCount);
}

#endif

#if COMPILE_EMITTER

RWByteAddressBuffer uCounters : register(u0);
RWStructuredBuffer<uint> uDeadList : register(u1);
RWStructuredBuffer<uint> uAliveList1 : register(u2);
RWStructuredBuffer<ParticleData> uParticleData  : register(u3);

[numthreads(128, 1, 1)]
void Emit(CS_INPUT input)
{
    uint emitCount = uCounters.Load(EMIT_COUNT);
    if(input.DispatchThreadId.x < emitCount)
    {
        ParticleData p;
        p.LifeTime = 0;
        p.Position = float3(0, 0, 0);
        p.Velocity = float3(0, 20, 0);

        uint deadSlot;
        uCounters.InterlockedAdd(DEAD_LIST_COUNTER, -1, deadSlot);
        uint particleIndex = uDeadList[deadSlot - 1];
        uParticleData[particleIndex] = p;

        uint aliveSlot;
        uCounters.InterlockedAdd(ALIVE_LIST_1_COUNTER, 1, aliveSlot);
        uAliveList1[aliveSlot] = particleIndex;
    }
}

#endif

#ifdef COMPILE_SIMULATE

cbuffer Parameters : register(b0)
{
    float cDeltaTime;
    float cParticleLifeTime;
}

RWByteAddressBuffer uCounters : register(u0);
RWStructuredBuffer<uint> uDeadList : register(u1);
RWStructuredBuffer<uint> uAliveList1 : register(u2);
RWStructuredBuffer<uint> uAliveList2 : register(u3);
RWStructuredBuffer<ParticleData> uParticleData : register(u4);

[numthreads(128, 1, 1)]
void Simulate(CS_INPUT input)
{
    uint aliveCount = uCounters.Load(ALIVE_LIST_1_COUNTER);
    if(input.DispatchThreadId.x < aliveCount)
    {
        uint particleIndex = uAliveList1[input.DispatchThreadId.x];
        ParticleData p = uParticleData[particleIndex];

        if(p.LifeTime < cParticleLifeTime)
        {
            p.Velocity += float3(0, -9.81f * cDeltaTime, 0);
            p.Position += p.Velocity * cDeltaTime;
            p.LifeTime += cDeltaTime;
            uParticleData[particleIndex] = p;

            uint aliveSlot;
            uCounters.InterlockedAdd(ALIVE_LIST_2_COUNTER, 1, aliveSlot);
            uAliveList2[aliveSlot] = particleIndex;
        }
        else
        {
            uint deadSlot;
            uCounters.InterlockedAdd(DEAD_LIST_COUNTER, 1, deadSlot);
            uDeadList[deadSlot] = particleIndex;
        }
    }
}

#endif

#ifdef COMPILE_SIMULATE_END

ByteAddressBuffer tCounters : register(t0);
RWByteAddressBuffer uArgumentsBuffer : register(u0);

[numthreads(1, 1, 1)]
void SimulateEnd(CS_INPUT input)
{
    uint particleCount = tCounters.Load(ALIVE_LIST_2_COUNTER);
    uArgumentsBuffer.Store4(0, uint4(6 * particleCount, 1, 0, 0));
}

#endif