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

#define ALIVE_LIST_1_COUNTER 0
#define ALIVE_LIST_2_COUNTER 1
#define DEAD_LIST_COUNTER 2
#define EMIT_COUNTER 3

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
    uint aliveParticleCount = uCounters.Load(ALIVE_LIST_1_COUNTER);

    uint emitCount = min(deadCount, cEmitCount);

    uEmitArguments.Store3(0, uint3(emitCount / 128, 1, 1));

    uSimulateArguments.Store3(0, uint3((aliveParticleCount + emitCount) / 128, 1, 1));

    uCounters.Store(ALIVE_LIST_1_COUNTER, aliveParticleCount);
    uCounters.Store(ALIVE_LIST_2_COUNTER, 0);
    uCounters.Store(EMIT_COUNTER, emitCount);
}

#endif

#if COMPILE_EMITTER

RWStructuredBuffer<uint> uDeadList : register(u0);
RWStructuredBuffer<uint> uAliveList1 : register(u1);
RWStructuredBuffer<ParticleData> uParticleData  : register(u2);

[numthreads(128, 1, 1)]
void Emit(CS_INPUT input)
{
    uint deadSlot = uDeadList.DecrementCounter();
    uint particleIndex = uDeadList[deadSlot - 1];

    ParticleData p = uParticleData[particleIndex];
    p.LifeTime = 0;
    p.Position = float3(input.DispatchThreadId.x, 0, 0);
    p.Velocity = float3(1, 0, 0);
    uParticleData[particleIndex] = p;

    uint aliveSlot = uAliveList1.IncrementCounter();
    uAliveList1[aliveSlot] = particleIndex;
}

#endif

#ifdef COMPILE_SIMULATE

cbuffer Parameters : register(b0)
{
    float cDeltaTime;
    float cParticleLifeTime;
}

RWStructuredBuffer<uint> uDeadList : register(u0);
RWStructuredBuffer<uint> uAliveList1 : register(u1);
RWStructuredBuffer<ParticleData> uParticleData  : register(u2);
StructuredBuffer<uint> uAliveList2  : register(t0);

[numthreads(128, 1, 1)]
void Simulate(CS_INPUT input)
{
    uint particleIndex = uAliveList2[input.DispatchThreadId.x];

    if(uParticleData[particleIndex].LifeTime < cParticleLifeTime)
    {
        ParticleData p = uParticleData[particleIndex];
        p.Position += p.Velocity * cDeltaTime;
        p.LifeTime += cDeltaTime;
        uParticleData[particleIndex] = p;

        uint aliveSlot = uAliveList1.IncrementCounter();
        uAliveList1[aliveSlot] = input.DispatchThreadId.x;
    }
    else
    {
        uint deadSlot = uDeadList.IncrementCounter();
        uDeadList[deadSlot] = input.DispatchThreadId.x;
    }
}

#endif