// Copyright © Earth Heroes 2024. Defend The Dungeon™ is a trademark of Earth Heroes. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Ability/Effect/DamageEffect.h"
#include "Component/Effect/HitEffectComponent.h"
#include "Components/ActorComponent.h"
#include "DefendTheDungeon/ETC/Enum/Enum.h"
#include "CombatComponent.generated.h"


DECLARE_DYNAMIC_DELEGATE(FActionCancelCalled);
DECLARE_DYNAMIC_DELEGATE(FActionCalled);
DECLARE_DYNAMIC_DELEGATE(FActionEnded);


class AIngamePlayerController;
enum class EHitEffectState : uint8;
class AGravityProjectile;
enum class EAttackType : uint8;
enum class EDamageType : uint8;
class ADecalActor;
class ADarkMagicOrbSkill;
class UProjectileShooterComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class AMonsterBase;
class ADDCharacter;

UENUM(Blueprintable)
enum EActionType
{
	Normal,
	Dead,
	CrowdControl,
	SmallKnockback,
	Skill,
	Attacking,
	Block,
};

USTRUCT(Blueprintable)
struct FAction
{
	GENERATED_BODY()

	//액션 소유 겍체
	UPROPERTY()
	UObject *Owner;

	//액션 타입
	UPROPERTY()
	TEnumAsByte<EActionType> ActionType;

	//실행 함수
	UPROPERTY()
	FName PlayFunctionName;

	//취소 함수
	UPROPERTY()
	FName CancelFunctionName;

	//엔드 함수
	UPROPERTY()
	FName EndFunctionName;

	//실행 우선순위
	UPROPERTY()
	int32 ActionLevel;

	//취소 우선순위
	UPROPERTY()
	int32 CancelLevel;

	//액션 이름, ActionName이 달라도 다른 속성이 동일한 경우, 같은 Action으로 취급합니다.
	UPROPERTY()
	FName ActionName;
	
	FAction()
		: Owner(nullptr)
		, ActionType(EActionType::Normal) // 기본값으로 Normal 설정 가정
		, PlayFunctionName(NAME_None)
		, CancelFunctionName(NAME_None)
		, EndFunctionName(NAME_None)
		, ActionLevel(100)   // 기본 우선순위(낮음) 예시
		, CancelLevel(100)   // 기본 취소 우선순위(낮음) 예시
		, ActionName(NAME_None)
	{
	}

	FAction(UObject *Owner, EActionType ActionType, FName PlayFunc, FName CancelFunc, FName EndFunc, int32 ActionLevel, int32 CancelLevel, FName ActionName)
		: Owner(Owner)
		, ActionType(ActionType) // 기본값으로 Normal 설정 가정
		, PlayFunctionName(PlayFunc)
		, CancelFunctionName(CancelFunc)
		, EndFunctionName(EndFunc)
		, ActionLevel(ActionLevel)   // 기본 우선순위(낮음) 예시
		, CancelLevel(CancelLevel)   // 기본 취소 우선순위(낮음) 예시
		, ActionName(ActionName)
	{
	}

	FAction(UObject *Owner, EActionType ActionType, FName PlayFunc, FName CancelFunc, FName EndFunc, int32 ActionLevel, int32 CancelLevel)
		: Owner(Owner)
		, ActionType(ActionType) // 기본값으로 Normal 설정 가정
		, PlayFunctionName(PlayFunc)
		, CancelFunctionName(CancelFunc)
		, EndFunctionName(EndFunc)
		, ActionLevel(ActionLevel)   // 기본 우선순위(낮음) 예시
		, CancelLevel(CancelLevel)   // 기본 취소 우선순위(낮음) 예시
		, ActionName(PlayFunc)
	{
	}
	
	FAction(UObject *Owner, FName PlayFunc)
		: Owner(Owner)
		, ActionType(EActionType::Normal) // 기본값으로 Normal 설정 가정
		, PlayFunctionName(PlayFunc)
		, CancelFunctionName(NAME_None)
		, EndFunctionName(NAME_None)
		, ActionLevel(100)   // 기본 우선순위(낮음) 예시
		, CancelLevel(100)   // 기본 취소 우선순위(낮음) 예시
		, ActionName(NAME_None)
	{
	}
	
	bool operator==(const FAction& Action) const
	{
		if (   ActionLevel == Action.ActionLevel
			&& CancelLevel == Action.CancelLevel
			&& Owner == Action.Owner
			&& ActionType == Action.ActionType
			&& PlayFunctionName == Action.PlayFunctionName
			&& CancelFunctionName == Action.CancelFunctionName
			&& EndFunctionName == Action.EndFunctionName)
			return true;

		return false;
	}
};


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class DEFENDTHEDUNGEON_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()
	friend  AGravityProjectile;

protected:
	
	UCombatComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	UPROPERTY()
	ADDCharacter *DDCharacter;
	UPROPERTY()
	UBaseStatComponent *StatComponent;
	UPROPERTY()
	AIngamePlayerController *IngamePlayerController;


	//Montages
	UPROPERTY(EditAnywhere, Category="Montage")
	TArray<UAnimMontage*> AttackAnimMontage;

	UPROPERTY(EditAnywhere, Category="Montage")
	TArray<UAnimMontage*> DashAnimMontage;

	UPROPERTY(EditAnywhere, Category="Montage")
	TArray<UAnimMontage*> SkillAnimMontage;

	UPROPERTY(EditAnywhere, Category="Montage")
	UAnimMontage* BlockMontage;

	UPROPERTY(EditAnywhere, Category="Montage")
	UAnimMontage *StunMontage;

	UPROPERTY(EditAnywhere, Category="Montage")
	TArray<UAnimMontage*> KnockBackMontage;

	UPROPERTY(EditAnywhere, Category="Montage")
	UAnimMontage *BigKnockBackMontage;

	//Particles
	UPROPERTY(EditAnywhere, Category="Effect")
	UParticleSystem *StunParticle;
	UPROPERTY(EditAnywhere, Category="Effect")
	UParticleSystem *BlockSuccessEffect2;
	UPROPERTY(EditAnywhere, Category="Effect")
	UParticleSystem *ShockParticle;

	
public:
	
	//초기 세팅
	void SetCharacter(ADDCharacter *AddCharacter) { DDCharacter = AddCharacter;}

	UFUNCTION(NetMulticast, Reliable)
	void SetIngameplayerController();
	
	/**
	 * 현재 캐릭터가 아이템을 장착할 수 있는 상태인지 검사합니다.
	 * @return 장착 가능하면 true, 불가능하면 false 반환.
	 */
	bool CanEquipItem() const;
	void SetSkillAnimMontage(UAnimMontage *NewMontage, int32 Index) {SkillAnimMontage[Index] = NewMontage;}

	//델리게이트
	UFUNCTION()
	void OnDamaged(AActor* InInstigator, float Damage, EAttackType DamageAttackType);

/*********************************************************/
/*
 행동(Action) 가상 함수 목록
 캐릭터가 할 수 있는 행동들은 우선 순위가 정해져 있다.
 
 하고자 할 행동의 우선 순위가 현재 하고 있는 행동보다 낮으면, 실행을 거절한다.
 만약 반대인 경우, 현재 행동을 중단하고 입력받은 행동을 실행한다.

우선순위 변수는 CancelLevel, ActionLevel이 있는데, 낮을 수록 높은 우선순위를 가진다.
취소 우선수위를 따로 다루는 이유는, 대부분의 Action이 실행 우선순위와 취소 우선순위가 다르기 때문이다.

예를 들면, 우리 게임의 경우 캐릭터의 스킬 시전 중, 다른 스킬을 사용할 수 없다.
하지만 Stun은 실행중인 스킬 액션을 취소하고, 스턴에 걸릴 수 있다. 

 가장 낮은 우선순위 행동은 idle, move등의 Default 상태 이며, 이 행동은 중단될 경우가 많다.
 가장 높은 우선순위 행동은 dead로, 다른 어떠한 행동도 할 수 없다.

 현재까지 대표적인 Action의 우선순위이다.
			ActionLev	CancelLev
	Dead		0			0
	Dash		3			2
	Attack		4			3
	Skill		3			2
	Block		3			5
	jump		3	        *
	Default	  Max(100)    Max(100)

	* : 디른 어떤 액션도 할 수 있지만 취소 되진 않는다.
 */

private:

	//현재 액션 레벨
	UPROPERTY(Replicated)
	FAction CurAction;

	//델리게이트
	FActionCalled ActionCalled;
	FActionCancelCalled ActionCancelCalled;
	FActionEnded ActionEnded;
	
	
	bool CheckValidAction(const FAction &Action) const;
	bool TryPlayAction_Internal(FAction &Action);

protected:
	UFUNCTION()
	void SetDefaultAction()
	{
		CurAction.Owner = nullptr;
		CurAction.ActionLevel = ActionMax;
		CurAction.CancelLevel = ActionMax;
		CurAction.ActionName = FName();
		CurAction.CancelFunctionName = CurAction.EndFunctionName = CurAction.PlayFunctionName = FName();

		ActionCalled.Clear();
		ActionCancelCalled.Clear();
	}
	
public:
	
	//Action Max
	static constexpr int32 ActionMax = 100;

	/**
	 * 플레이어가 특정 액션을 지금 할 수 있는지 검사합니다.
	 * 
	 * @param ActionLevel 캐릭터가 시도하려는 액션의 우선순위입니다.
	 * 숫자가 낮을수록 우선순위가 높으며, 0이 최고 우선순위, ActionMax가 최하위 우선순위입니다.
	 * 현재 실행 중인 액션의 CancelLevel과 비교하여 허용 여부를 판정합니다.
	 * 
	 * @return 현재 상태에서 주어진 ActionLevel 액션을 수행할 수 있으면 true, 그렇지 않으면 false 반환.
	*/
	bool CanPlayAction(const int32 ActionLevel) const;

	/**
	 * 클라이언트에서 액션 실행을 요청할 때 호출하는 서버 RPC 함수입니다.
	 * 
	 * @param Action 실행을 요청하는 액션 구조체 정보입니다.
	 * 
	 * 서버 전용, 신뢰 가능한 (Reliable) 호출이어야 하며, 네트워크 환경에서 클라이언트가 액션 수행 요청을 할 때 사용합니다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_TryPlayAction(FAction Action);

	/**
	 * 액션의 우선순위, 유효성, 현재 상태 등을 점검하여 액션 수행 여부를 결정합니다.
	 * @param Action 실행할 액션 구조체.
	 */
	void TryPlayAction(FAction &Action);

	/**
	 * 액션 실행을 시도하는 편리한 오버로드 함수입니다.
	 * 액션에 필요한 모든 정보를 인자로 받아 FAction 구조체를 생성하고 내부 실행 함수로 전달합니다.
	 * 
	 * @param Owner 액션을 실행할 UObject 소유자.
	 * @param PlayFunctionName 실행할 액션 함수명.
	 * @param ActionLevel 액션의 우선순위(0이 최고, ActionMax가 최하).
	 * @param CancelLevel 이 액션이 취소할 수 있는 우선순위 범위.
	 * @param EndActionName 액션 종료 시 호출할 함수명.
	 * @param CancelFunctionName 액션 취소 시 호출할 함수명.
	 * @param ActionType 액션 유형 (기본값 Normal).
	 * @param ActionName 액션 이름 (지정하지 않으면 PlayFunctionName 사용).
	 */
	void TryPlayAction(UObject *Owner, FName PlayFunctionName = FName(), int32 ActionLevel = ActionMax, int32 CancelLevel = ActionMax,  FName EndActionName = FName(), FName CancelFunctionName = FName(), EActionType ActionType = EActionType::Normal, FName ActionName = FName());

	/**
	 * 지정한 액션을 강제로 실행하는 함수입니다.
	 * 현재 액션 상태와 무관하게 우선순위를 0으로 설정하여 즉시 실행합니다.
	 * 웬만하면 TryPlayAction을 사용할 것을 추천드립니다.
	 * 
	 * @param Action 강제로 실행할 액션 구조체.
	 */
	void ForcePlayAction(FAction &Action);

	/**
	 * 현재 캐릭터가 수행 중인 액션 정보를 가져옵니다.
	 * 
	 * @return 현재 액션 구조체.
	 */
	FAction GetCurAction() const {return CurAction;}

/*
 1. 행동 시작 함수
 플레이어 입력 시 호출되는 함수
 -  애니메이션 유효 검사
 - 시전 시 처리해야 할 행동
 */
	UFUNCTION(BlueprintCallable)
	virtual bool Attack();
	
	void Dash();
	UFUNCTION(Server, Reliable)
	virtual void Server_Dash(ENoWeaponDash InDashSide);
	
	UFUNCTION(BlueprintCallable)
	virtual bool SkillQ();

	UFUNCTION(BlueprintCallable)
	virtual bool SkillE();

	UFUNCTION(BlueprintCallable)
	virtual bool SkillR();

	UFUNCTION(BlueprintCallable)
	virtual void SkillR_KeyDown();

	UFUNCTION(BlueprintCallable)
	virtual void SkillQ_Confirm();
	
	UFUNCTION(BlueprintCallable)
	virtual void Skill_Confirm(int SkillCommand);

	UFUNCTION(Server, Reliable)
	void Server_Skill_Confirm_SubWeapon();
	
	void Stun(float Duration);
	void Block();
	void ShockCharacter(float Duration);
	void BigKnockBackCharacter();
	void KnockBackCharacter();

/*
 2. 액션 함수
//TryPlayAction에서 유효 검사를 모두 통과 시, 처음 실행하는 함수
 - 애니메이션 재생
 - 스킬 초기 변수 설정
 */
	
	UFUNCTION()
	virtual void Attack_Action();

	UFUNCTION()
	virtual void SkillQ_Action();
	
	UFUNCTION()
	virtual void SkillE_Action();
	
	UFUNCTION()
	virtual void SkillR_Action();
	//
	UFUNCTION()
	virtual void Dash_Action();
	
	UFUNCTION()
	virtual void Block_Action();
	

/*
 2. 행동 중간 함수
 행동 중, 중간에 해야할 일을 정의하는 함수, 애님 몽타주 중, AnimNotify, AnimNotifyState로 호출된다.
 AnimNotify 목록(NS_HitDetection, NS_ShootProjectile, NS_ClientTickUpdate, AN_ESkill)
 - 주로 적 쿼리를 위한 Trace 후 공격을 구현
 - 발사체 발사
 - 애니메이션 특정 구간 동안 UI 처리
 */
	UFUNCTION()
	virtual void DetectedHit();

	UFUNCTION()
	virtual void SkillQDetectedHit();

	UFUNCTION()
	virtual void SkillEDetectedHit();

	UFUNCTION()
	virtual void SkillRDetectedHit();

	UFUNCTION()
	virtual void ShootProjectile();
	
	UFUNCTION()
	virtual void Client_TickUpdate();
	UFUNCTION(BlueprintCallable)
	virtual void Client_TickUpdateEnd();

	UFUNCTION()
	virtual void SubWeaponSkill();


/*
 3. 행동 끝 함수
 행동이 온전히 끝나면 호출되는 함수, 애님 몽타주 중, AnimNotify, AnimNotifyState로 호출된다.
 온전히 끝나지 않으면 중단 함수 호출
 */
	UFUNCTION()
	virtual void AttackEnd();
	UFUNCTION()
	virtual void SkillQEnd();
	UFUNCTION()
	virtual void SkillEEnd();
	UFUNCTION()
	virtual void SkillREnd();
	UFUNCTION()
	void DashEnd();
	UFUNCTION()
	void BlockEnd();

/*
 4. 중단 함수
 행동 검사 함수에 의해 호출, 행동 중 중단 시 호출된다.
 - 행동 중단 시, 각 행동에 대한 처리
 */
	UFUNCTION()
	virtual void Attack_Cancel();
	
	UFUNCTION()
	virtual void SkillQ_Cancel();

	UFUNCTION()
	virtual void SkillE_Cancel();

	UFUNCTION()
	virtual void SkillR_Cancel();

	UFUNCTION()
	void Dash_Cancel();

	UFUNCTION()
	void Block_Cancel();

	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	virtual void StopAllMontages();

	UFUNCTION(BlueprintCallable, NetMulticast, Reliable)
	void StopMontage(float BlendOut, UAnimMontage *Montage);

	//===============================
	//애니메이션 재생
	UFUNCTION(NetMulticast, Reliable)
	virtual void MC_PlayAttackMontage(int32 CurComboCount);
	UFUNCTION(NetMulticast, Reliable)
	virtual void MC_PlayQMontage();
	UFUNCTION(NetMulticast, Reliable)
	virtual void MC_PlayEMontage();
	UFUNCTION(NetMulticast, Reliable)
	virtual void MC_PlayRMontage();
	
	UFUNCTION(NetMulticast, Reliable)
	void MC_Dash(ENoWeaponDash DashDirection);
	UFUNCTION(NetMulticast, Reliable)
	void MC_Block();
	UFUNCTION(NetMulticast, Reliable)
	void MC_BlockSuccess();

	//===============================
	//ETC 함수
	
	//UI에 필요한 함수
	FORCEINLINE float GetSpread() const { return ProjectileSpread; }
	
	//카메라를 움직일 수 있는지
	UFUNCTION(Client, Reliable)
	void CL_SetbCanLook(bool bNewCanLook);

	//공격 시 초기화 카운트 초기화 함수
	void ResetAttackCombo();
	virtual void ResetSkillCombo();

	UFUNCTION(NetMulticast, Reliable)
	void MC_SetWalkSpeed(float walkspeed);
	
	/**
	 * 지정된 구체 범위 내에서 지정한 오브젝트 타입들에 대해 다중 충돌 검사(Sphere Trace)를 수행합니다.
	 * 
	 * @param StartLocation 충돌 검사 시작 위치.
	 * @param EndLocation 충돌 검사 종료 위치.
	 * @param SphereRadius 검사 구체 반경.
	 * @param HitResults 검사 결과로 반환할 히트 정보 배열(중복 제거 후 추가됨).
	 * @param bTraceCharacter 캐릭터 충돌 검사 여부(true면 캐릭터 채널 포함).
	 * 
	 * @return 검사 결과 중 유효 충돌이 하나라도 있으면 true, 없으면 false 반환.
	 * 
	 * @note 이 함수는 서버에서 호출되어야 하며, 서버 권한 하에서만 신뢰성 있게 동작하도록 설계됨.
	 * 충돌 채널은 게임 전용 채널, 월드 정적/동적 오브젝트, 캐릭터 채널 등을 포함하도록 구성됨.
	 * 
	 * 충돌된 액터들 중 지정된 캐릭터, 몬스터, 데미지 가능 오브젝트, 스킬 오브젝트, 빌딩 베이스 액터만 결과에 포함됩니다.
	 * 중복 액터는 필터링하여 한 번만 결과에 추가합니다.
	 * 충돌이 감지되면 조준선 반응(CrosshairHitReaction)을 호출합니다.
	 */
	bool SphereTrace(FVector StartLocation, FVector EndLocation, float SphereRadius, TArray<FHitResult> &HitResults, bool bTraceCharacter = false);
	
	void CrosshairHitReaction();
	
	/**
	 * 대상 액터에 공격 피해를 적용하는 함수입니다.
	 * 
	 * @param TargetActor 피해를 입힐 대상 액터.
	 * @param AdScale 물리 공격력 배율.
	 * @param ApScale 마법 공격력 배율.
	 * @param bHasKnockback 넉백 효과 적용 여부.
	 * @param DamageType 피해 유형 (물리, 마법 등).
	 * @param AttackType 공격 유형 (일반 공격, 스킬 등).
	 * @param SkillName 공격/스킬 이름.
	 * @param HitEffectState 피격 이펙트 상태.
	 * @param HitLocation 피격 위치 정보 (ZeroVector면 액터 위치 기준).
	 * 
	 * 일반 공격일 경우 캐릭터의 OnNormalAttackHit 델리게이트를 브로드캐스트합니다.
	 * 대상이 몬스터라면 피격 위치를 기준으로 피격 이펙트를 재생하며, 넉백 여부와 공격 타입, 스킬명도 전달합니다.
	 * ADamageableActor 유형 대상이면 별도 Damaged 함수를 호출하고 피해 적용 과정을 종료합니다.
	 * 대상이 지정된 타입이 아니면 피해 적용을 하지 않고 리턴합니다.
	 */
	void ApplyCombatDamage(AActor* TargetActor, float AdScale = 1.f, float ApScale = 1.f, bool bHasKnockback = false, EDamageType DamageType = EDamageType::AdDamage, EAttackType AttackType = EAttackType::NormalAttack, FName SkillName = TEXT("None"), EHitEffectState HitEffectState = EHitEffectState::None, FVector HitLocation = FVector::ZeroVector);

	/**
	 * 화면 크로스헤어에 맞춰 발사할 프로젝타일의 위치와 회전 값을 계산합니다.
	 * 
	 * @param Location 프로젝타일이 생성될 위치. 캐릭터의 "SkillActorSpawn" 소켓 위치를 기본으로 사용합니다.
	 * @param Rotation 프로젝타일의 발사 회전 값. 크로스헤어가 가리키는 방향 또는 맞춤 위치를 기반으로 설정됩니다.
	 * @param bHaveGravity 프로젝타일에 중력 영향 여부. 중력 영향을 받는 경우 발사 각도를 약간 조정합니다.
	 * 
	 * @return 발사 경로 상에 충돌체가 감지되면 true, 아니면 false 반환.
	 */
	bool FindTransformToShootProjectile(FVector &Location, FRotator &Rotation, const bool bHaveGravity = false) const;
	

public:
/* E 스킬 구현 */
	//쉴드 스킬 : 도발
	void ShieldProvocation();

	//단검 스킬 : 은신
	void SwordHiding();
	void EndStealth();
	void SetStealth();
	UFUNCTION(NetMulticast, Reliable)
	void MC_SetStealth(bool bStealth);

	//마법 오브 스킬 : 보호막
	void GiveShieldReady();
	UFUNCTION(NetMulticast, Reliable)
	void SetComponentTick(bool bTick);

	UFUNCTION(NetMulticast, Reliable)
	void MC_ConfirmGiveShield();
	void GiveShield();
	void ApplyBarrier(AActor* NewInstigator, float Amount);

	UFUNCTION(Client, Reliable)
	void CL_SetOverlayInst(bool bSet);

	//암흑 마법 오브 스킬 : 범위 디버프
	void DarkMagicOrbSkill();
	void DarkMagicOrbSkillRun();

	//지팡이 스킬 : 중력구체
	void GravityProjectile();
	
	
	//없앨 함수들 목록
public:
	UFUNCTION(Blueprintable)
	void ResetBoolValByKnockbacked();
	UPROPERTY()
	UNiagaraComponent *NiagaraComponent;
	void ResetValue();
	//스킬을 방해할 때 명시적 호출
	void SkillInterrupted(UAnimMontage *AnimMontage);
	//공격 시 초기화해야 하는 변수들을 초기화 하는 함수
	void ResetBeforeAttack();

/*******************************************************************/
/*
 상태(State)
 상태는 행동의 결과
 행동 검사 시 사용
 */
protected:
	//모든 캐릭터 공통 : 대쉬 애니메이션 실행 중 true.
	UPROPERTY(BlueprintReadWrite)
	bool bIsDashing = false;
	
	//모든 캐릭터 공통 : 공격 애니메이션 or 공격 관련 코드 실행 중 true.
	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bIsAttacking = false;
	
	//모든 캐릭터 공통 : SkillQ 수행 중 true.
	UPROPERTY(BlueprintReadWrite)
	bool bSkillQ = false;

	//모든 캐릭터 공통 : SKillE 수행 중 true.
	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bSkillE = false;
	
	//모든 캐릭터 공통 : SKillR 수행 중 true.
	UPROPERTY(BlueprintReadWrite)
	bool bSkillR = false;

	//모든 캐릭터 공통 : 스킬 준비 완료시 true.
	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bQReady = false;

	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bEReady = false;

	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bRReady = false;

	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bBlockReady = true;
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bDashReady = false;

	//상태 불 변수
	UPROPERTY(BlueprintReadWrite)
	bool bStealthed = false;
	
	UPROPERTY(BlueprintReadWrite, Replicated)
	bool bAimingToGiveShield = false;

	UPROPERTY(BlueprintReadWrite)
	bool bDarkMagicOrbSkill = false;

	UPROPERTY(BlueprintReadWrite)
	bool bGravityProjectileShooted = false;

	UPROPERTY(BlueprintReadWrite)
	bool bIsBlocking;

	UPROPERTY(BlueprintReadWrite)
	bool bShocked = false;

	UPROPERTY(BlueprintReadWrite)
	bool bStunned = false;

	UPROPERTY(BlueprintReadWrite)
	bool bKnockbacked = false;

	UPROPERTY(BlueprintReadWrite)
	bool bBigKnockbacked = false;
	
public:
    bool IsDashing() const { return bIsDashing; }
    bool IsAttacking() const { return bIsAttacking; }
    bool IsSkillQActive() const { return bSkillQ; }
    bool IsSkillEActive() const { return bSkillE; }
    bool IsSkillRActive() const { return bSkillR; }
    bool IsQReady() const { return bQReady; }
    bool IsEReady() const { return bEReady; }
    bool IsRReady() const { return bRReady; }
    bool IsBlockReady() const { return bBlockReady; }
    bool IsDashReady() const { return bDashReady; }
    bool IsStealthed() const { return bStealthed; }
    bool IsAimingToGiveShield() const { return bAimingToGiveShield; }
    bool IsDarkMagicOrbSkillActive() const { return bDarkMagicOrbSkill; }
    bool IsGravityProjectileShooted() const { return bGravityProjectileShooted; }
    bool IsBlocking() const { return bIsBlocking; }
    bool IsShocked() const { return bShocked; }
    bool IsStunned() const { return bStunned; }
    bool IsKnockbacked() const { return bKnockbacked; }
    bool IsBigKnockbacked() const { return bBigKnockbacked; }

public:
	bool CanBeSeen() const {return !bStealthed;}


protected:
/*******************************************************************/
/*
 쿨타임 (CoolTime)
 행동 가능 함수 내부에서 쿨타임 검사를 먼저 실시한다.
 */
	
	UPROPERTY(Replicated)
	float QCoolTime = 5.f;

	UPROPERTY(Replicated)
	float ECoolTime = 5.f;

	UPROPERTY(Replicated)
	float RCoolTime = 5.f;

	UPROPERTY(Replicated)
	float BlockCoolTime = 1.5f;

	UPROPERTY(Replicated)
	float DashCoolTime = 5.f;

protected:
	void StartQCoolTime();
	void StartECoolTime();
	void StartRCoolTime();
	void StartDashCoolTime();
	
	UFUNCTION(Client, Reliable)
	void Client_StartQCoolTime();
	UFUNCTION(Client, Reliable)
	void Client_StartECoolTime();
	UFUNCTION(Client, Reliable)
	void Client_StartRCoolTime();
	UFUNCTION(Client, Reliable)
	void Client_StartBlockCoolTime();
	UFUNCTION(Client, Reliable)
	void Client_StartDashCoolTime();
	
public:
	void StartBlockCoolTime();

	void SetQCoolTime(const float InCoolTime) {QCoolTime = InCoolTime;}
	void SetECoolTime(const float InCoolTime) {ECoolTime = InCoolTime;}
	void SetRCoolTime(const float InCoolTime) {RCoolTime = InCoolTime;}
	void SetDashCoolTime(const float InCoolTime) {DashCoolTime = InCoolTime;}
	void SetBlockCoolTime(const float InCoolTime) {BlockCoolTime = InCoolTime;}

protected:
	void QReady();
	void EReady();
	void RReady();
	void BlockReady();
	void DashReady();

/*******************************************************************/
/*
 액션 스텟 (Action Stat)
 스킬, 공격에 붙어있는 고유 스텟, StatComponent에서 나온 스텟 결과물과 곱해진다.
 ex) 대검 Q는 초당 50%의 데미지를 준다 => QAdRatio = 0.5f,
	 초당 데미지 ->  QAdRatio * StatComp->GetFinalDamage(AD)
 */
	
	//캐릭터 공격 속도(임시), 서버에서 설정해야 함
	UPROPERTY(Replicated)
	float AttackSpeed = 1.f;

	//캐릭터 스킬 계수(임시)
	UPROPERTY()
	float QAdRatio = 1.f;

	UPROPERTY()
	float QApRatio = 1.f;

	UPROPERTY()
	float EAdRatio = 1.f;

	UPROPERTY()
	float EApRatio = 1.f;

	UPROPERTY()
	float RAdRatio = 1.f;
	
	UPROPERTY()
	float RApRatio = 1.f;

public:
	void SetQAdRatio(const float InRatio) {QAdRatio = InRatio;}
	void SetEAdRatio(const float InRatio) {EAdRatio = InRatio;}
	void SetRAdRatio(const float InRatio) {RAdRatio = InRatio;}
	void SetQApRatio(const float InRatio) {QApRatio = InRatio;}
	void SetEApRatio(const float InRatio) {EApRatio = InRatio;}
	void SetRApRatio(const float InRatio) {RApRatio = InRatio;}
	
	float GetQAdRatio() const { return QAdRatio; }
	float GetQApRatio() const { return QApRatio; }
	float GetEAdRatio() const { return EAdRatio; }
	float GetEApRatio() const { return EApRatio; }
	float GetRAdRatio() const { return RAdRatio; }
	float GetRApRatio() const { return RApRatio; }
	void SetAttackSpeed(float InAttackSpeed) {AttackSpeed = InAttackSpeed;}
/*
 임시 객체, 정보들
 CombatComponent 내에서 구현을 위해 잠시 저장된 객체나 정보들
 상황에 따라 유효하지 않을 수 있으며, 접근 시 유효성 검사를 꼭 해야한다.
 */
	//Enum
	UPROPERTY()
	ENoWeaponDash DashSide;
	
	//Object
	UPROPERTY()
	AGravityProjectile *ShootedGravityProjectile;
	UPROPERTY()
	ADarkMagicOrbSkill *DecalMagicOrbSkill;
	UPROPERTY()
	ADDCharacter *OverlayedCharacter;
	UPROPERTY()
	UProjectileShooterComponent *ProjectileShooterComp;

	UPROPERTY()
	UParticleSystemComponent *StunComp;
	UPROPERTY()
	UParticleSystemComponent *ShockComp;

	//TimerHandler
	FTimerHandle StealthHandle;
	FTimerHandle DarkMagicOrbHandle;
	
	FTimerHandle AttackComboHandle;
	FTimerHandle SkillComboHandle;

	FTimerHandle QCoolTimeHandle;
	FTimerHandle ECoolTimeHandle;
	FTimerHandle RCoolTimeHandle;
	FTimerHandle BlockCoolTimeHandle;
	FTimerHandle DashCoolTimeHandle;
	
	FTimerHandle StunTimerHandle;
	FTimerHandle ShockTimerHandle;

	//Materials
	UPROPERTY()
	TArray<UMaterialInterface*> VisibleMaterials;
	UPROPERTY(EditAnywhere)
	TObjectPtr<UMaterialInterface> StealthMaterial;


	//Class
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Decal")
	TSubclassOf<ADecalActor> BP_MagicOrbDecal;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Projectile")
	TSubclassOf<AGravityProjectile> BP_GravityProjectile;

	//Info
	FVector DecalLocation;
	
	//stored integer, float
	float DarkMagicOrbTime = 0.f;
	int ComboCount = 0;

	UPROPERTY(Replicated)
	int SkillComboCount = 0;
	
	//Crosshair, 값이 커지면 벌어지고, 값이 작아지면 줄어든다.
	float ProjectileSpread = 0.f;
	
	//Tick안에서 Change 중일때 true
	bool bChangingOverlay = false;
	bool bIsBlockValid = false;



	UFUNCTION(NetMulticast, Reliable)
	void MC_StunMontage(bool binStunned);
	
	
	UFUNCTION(NetMulticast, Reliable)
	void MC_ShockParticle(bool bInShocked);
	
	UFUNCTION(NetMulticast, Reliable)
	void MC_BigKnockBackMontage();
	
	UFUNCTION(NetMulticast, Reliable)
	void MC_KnockBackMontage(int RandNumber);

	int KnockBackRandIndex;
};